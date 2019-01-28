/*
 * Copyright 2015-present Facebook. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include "fruid.h"

#define FIELD_TYPE(x)     ((x & (0x03 << 6)) >> 6)
#define FIELD_LEN(x)      (x & ~(0x03 << 6))
#define FIELD_EMPTY       "N/A"
#define NO_MORE_DATA_BYTE 0xC1

/* Unix time difference between 1970 and 1996. */
#define UNIX_TIMESTAMP_1996   820454400

/* Array for BCD Plus definition. */
const char bcd_plus_array[] = "0123456789 -.XXX";

/* Array for 6-Bit ASCII definition. */
const char * ascii_6bit[4] = {
  " !\"#$%&'()*+,-./",
  "0123456789:;<=>?",
  "@ABCDEFGHIJKLMNO",
  "PQRSTUVWXYZ[\\]^_"
};

const int field_chassis_opt_size = sizeof(fruid_field_chassis_opt) / sizeof(fruid_field_chassis_opt[0]);
const int field_board_opt_size = sizeof(fruid_field_board_opt) / sizeof(fruid_field_board_opt[0]);
const int field_product_opt_size = sizeof(fruid_field_product_opt) / sizeof(fruid_field_product_opt[0]);

/*
 * calculate_time - calculate time from the unix time stamp stored
 *
 * @mfg_time    : Unix timestamp since 1996
 *
 * returns char * for mfg_time_str
 * returns NULL for memory allocation failure
 */
static char * calculate_time(uint8_t * mfg_time)
{
  int len;
  struct tm * local;
  time_t unix_time = 0;
  unix_time = ((mfg_time[2] << 16) + (mfg_time[1] << 8) + mfg_time[0]) * 60;
  unix_time += UNIX_TIMESTAMP_1996;

  local = localtime(&unix_time);
  char * str = asctime(local);

  len = strlen(str);

  char * mfg_time_str = (char *) malloc(len);
  if (!mfg_time_str) {
#ifdef DEBUG
    syslog(LOG_WARNING, "fruid: malloc: memory allocation failed\n");
#endif
    return NULL;
  }

  memset(mfg_time_str, 0, len);

  memcpy(mfg_time_str, str, len);

  mfg_time_str[len - 1] = '\0';

  return mfg_time_str;
}

/*
 * verify_chksum - verify the zero checksum of the data
 *
 * @area        : offset of the area
 * @len         : len of the area in bytes
 * @chksum_read : stored checksum in the data.
 *
 * returns 0 if chksum is verified
 * returns -1 if there exist a mismatch
 */
static int verify_chksum(uint8_t * area, uint8_t len, uint8_t chksum_read)
{
  int i;
  uint8_t chksum = 0;

  for (i = 0; i < len - 1; i++)
    chksum += area[i];

  /* Zero checksum calculation */
  chksum = ~(chksum) + 1;

  return (chksum == chksum_read) ? 0 : -1;
}

/*
 * get_chassis_type - get the Chassis type
 *
 * @type_hex  : type stored in the data
 *
 * returns char ptr for chassis type string
 * returns NULL if type not in the list
 */
static char * get_chassis_type(uint8_t type_hex)
{
  int type, ret;
  char type_int[4];

  ret = sprintf(type_int, "%u", type_hex);
  type = atoi(type_int) - 1;

  /* If the type is not in the list defined.*/
  if (type > FRUID_CHASSIS_TYPECODE_MAX || type < FRUID_CHASSIS_TYPECODE_MIN) {
#ifdef DEBUG
    syslog(LOG_INFO, "fruid: chassis area: invalid chassis type\n");
#endif
    return NULL;
  }

  char * type_str = (char *) malloc(strlen(fruid_chassis_type[type])+1);
  if (!type_str) {
#ifdef DEBUG
    syslog(LOG_WARNING, "fruid: malloc: memory allocation failed\n");
#endif
    return NULL;
  }

  memcpy(type_str, fruid_chassis_type[type], strlen(fruid_chassis_type[type]));
  type_str[strlen(fruid_chassis_type[type])] = '\0';

  return type_str;
}

/*
 * _fruid_area_field_read - read the field data
 *
 * @offset    : offset of the field
 *
 * returns char ptr for the field data string
 */
static char * _fruid_area_field_read(uint8_t *offset)
{
  int field_type, field_len, field_len_eff;
  int idx, idx_eff, val;
  char * field;

  /* Bits 7:6 */
  field_type = FIELD_TYPE(offset[0]);
  /* Bits 5:0 */
  field_len = FIELD_LEN(offset[0]);

  /* Calculate the effective length of the field data based on type stored. */
  switch (field_type) {

  case TYPE_BINARY:
    /* TODO: Need to add support to read data stored in binary type. */
    field_len_eff = 1;
    break;

  case TYPE_ASCII_6BIT:
    /*
     * Every 3 bytes have four 6-bit packed values
     * + 6-bit values from the remaining field bytes.
     */
    field_len_eff = (field_len / 3) * 4 + (field_len % 3);
    break;

  case TYPE_BCD_PLUS:
  case TYPE_ASCII_8BIT:
    field_len_eff = field_len;
    break;
  }

  /* If field data is zero, store 'N/A' for that field. */
  field_len_eff > 0 ? (field = (char *) malloc(field_len_eff + 1)) :
                      (field = (char *) malloc(strlen(FIELD_EMPTY)));
  if (!field) {
#ifdef DEBUG
    syslog(LOG_WARNING, "fruid: malloc: memory allocation failed\n");
#endif
    return NULL;
  }

  memset(field, 0, field_len + 1);

  if (field_len_eff < 1) {
    strcpy(field, FIELD_EMPTY);
    return field;
  }

  /* Retrieve field data depending on the type it was stored. */
  switch (field_type) {
  case TYPE_BINARY:
   /* TODO: Need to add support to read data stored in binary type. */
    break;

  case TYPE_BCD_PLUS:

    idx = 0;
    while (idx != field_len) {
      field[idx] = bcd_plus_array[offset[idx + 1] & 0x0F];
      idx++;
    }
    field[idx] = '\0';
    break;

  case TYPE_ASCII_6BIT:

    idx_eff = 0, idx = 1;

    while (field_len > 0) {

      /* 6-Bits => Bits 5:0 of the first byte */
      val = offset[idx] & 0x3F;
      field[idx_eff++] = ascii_6bit[(val & 0xF0) >> 4][val & 0x0F];
      field_len--;

      if (field_len > 0) {
        /* 6-Bits => Bits 3:0 of second byte + Bits 7:6 of first byte. */
        val = ((offset[idx] & 0xC0) >> 6) |
              ((offset[idx + 1] & 0x0F) << 2);
        field[idx_eff++] = ascii_6bit[(val & 0xF0) >> 4][val & 0x0F];
        field_len--;
      }

      if (field_len > 0) {
        /* 6-Bits => Bits 1:0 of third byte + Bits 7:4 of second byte. */
        val = ((offset[idx + 1] & 0xF0) >> 4) |
              ((offset[idx + 2] & 0x03) << 4);
        field[idx_eff++] = ascii_6bit[(val & 0xF0) >> 4][val & 0x0F];

        /* 6-Bits => Bits 7:2 of third byte. */
        val = ((offset[idx + 2] & 0xFC) >> 2);
        field[idx_eff++] = ascii_6bit[(val & 0xF0) >> 4][val & 0x0F];

        field_len--;
        idx += 3;
      }
    }
    /* Add Null terminator */
    field[idx_eff] = '\0';
    break;

  case TYPE_ASCII_8BIT:

    memcpy(field, offset + 1, field_len);
    /* Add Null terminator */
    field[field_len] = '\0';
    break;
  }

  return field;
}

/* Free all the memory allocated for fruid information */
void free_fruid_info(fruid_info_t * fruid)
{
  if (fruid->chassis.flag) {
    free(fruid->chassis.type_str);
    free(fruid->chassis.part);
    free(fruid->chassis.serial);
    free(fruid->chassis.custom1);
    free(fruid->chassis.custom2);
    free(fruid->chassis.custom3);
    free(fruid->chassis.custom4);
  }

  if (fruid->board.flag) {
    free(fruid->board.mfg_time_str);
    free(fruid->board.mfg);
    free(fruid->board.name);
    free(fruid->board.serial);
    free(fruid->board.part);
    free(fruid->board.fruid);
    free(fruid->board.custom1);
    free(fruid->board.custom2);
    free(fruid->board.custom3);
    free(fruid->board.custom4);
  }

  if (fruid->product.flag) {
    free(fruid->product.mfg);
    free(fruid->product.name);
    free(fruid->product.part);
    free(fruid->product.version);
    free(fruid->product.serial);
    free(fruid->product.asset_tag);
    free(fruid->product.fruid);
    free(fruid->product.custom1);
    free(fruid->product.custom2);
    free(fruid->product.custom3);
    free(fruid->product.custom4);
  }
}

/* Initialize the fruid information struct */
static void init_fruid_info(fruid_info_t * fruid)
{
  fruid->chassis.flag = 0;
  fruid->board.flag = 0;
  fruid->product.flag = 0;
  fruid->chassis.type_str = NULL;
  fruid->chassis.part = NULL;
  fruid->chassis.serial = NULL;
  fruid->chassis.custom1 = NULL;
  fruid->chassis.custom2 = NULL;
  fruid->chassis.custom3 = NULL;
  fruid->chassis.custom4 = NULL;
  fruid->board.mfg_time_str = NULL;
  fruid->board.mfg = NULL;
  fruid->board.name = NULL;
  fruid->board.serial = NULL;
  fruid->board.part = NULL;
  fruid->board.fruid = NULL;
  fruid->board.custom1 = NULL;
  fruid->board.custom2 = NULL;
  fruid->board.custom3 = NULL;
  fruid->board.custom4 = NULL;
  fruid->product.mfg = NULL;
  fruid->product.name = NULL;
  fruid->product.part = NULL;
  fruid->product.version = NULL;
  fruid->product.serial = NULL;
  fruid->product.asset_tag = NULL;
  fruid->product.fruid = NULL;
  fruid->product.custom1 = NULL;
  fruid->product.custom2 = NULL;
  fruid->product.custom3 = NULL;
  fruid->product.custom4 = NULL;
}

/* Parse the Product area data */
int parse_fruid_area_product(uint8_t * product,
      fruid_area_product_t * fruid_product)
{
  int ret, index;

  index = 0;

  /* Reset the struct to zero */
  memset(fruid_product, 0, sizeof(fruid_area_product_t));

  /* Check if the format version is as per IPMI FRUID v1.0 format spec */
  fruid_product->format_ver = product[index++];
  if (fruid_product->format_ver != FRUID_FORMAT_VER) {
#ifdef DEBUG
    syslog(LOG_ERR, "fruid: product_area: format version not supported");
#endif
    return EPROTONOSUPPORT;
  }

  fruid_product->area_len = product[index++] * FRUID_AREA_LEN_MULTIPLIER;
  fruid_product->lang_code = product[index++];

  fruid_product->chksum = product[fruid_product->area_len - 1];
  ret = verify_chksum((uint8_t *) product,
          fruid_product->area_len, fruid_product->chksum);

  if (ret) {
#ifdef DEBUG
    syslog(LOG_ERR, "fruid: product_area: chksum not verified.");
#endif
    return EBADF;
  }

  fruid_product->mfg = _fruid_area_field_read(&product[index]);
  if (fruid_product->mfg == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  fruid_product->name = _fruid_area_field_read(&product[index]);
  if (fruid_product->name == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  fruid_product->part = _fruid_area_field_read(&product[index]);
  if (fruid_product->part == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  fruid_product->version = _fruid_area_field_read(&product[index]);
  if (fruid_product->version == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  fruid_product->serial = _fruid_area_field_read(&product[index]);
  if (fruid_product->serial == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  fruid_product->asset_tag = _fruid_area_field_read(&product[index]);
  if (fruid_product->asset_tag == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  fruid_product->fruid = _fruid_area_field_read(&product[index]);
  if (fruid_product->fruid == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (product[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_product->custom1 = _fruid_area_field_read(&product[index]);
  if (fruid_product->custom1 == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (product[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_product->custom2 = _fruid_area_field_read(&product[index]);
  if (fruid_product->custom2 == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (product[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_product->custom3 = _fruid_area_field_read(&product[index]);
  if (fruid_product->custom3 == NULL)
    return ENOMEM;
  index += FIELD_LEN(product[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (product[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_product->custom4 = _fruid_area_field_read(&product[index]);
  if (fruid_product->custom4 == NULL)
    return ENOMEM;

  return 0;
}

/* Parse the Board area data */
int parse_fruid_area_board(uint8_t * board,
      fruid_area_board_t * fruid_board)
{
  int ret, index, i;
  time_t unix_time;

  index = 0;

  /* Reset the struct to zero */
  memset(fruid_board, 0, sizeof(fruid_area_board_t));

  /* Check if the format version is as per IPMI FRUID v1.0 format spec */
  fruid_board->format_ver = board[index++];
  if (fruid_board->format_ver != FRUID_FORMAT_VER) {
#ifdef DEBUG
    syslog(LOG_ERR, "fruid: board_area: format version not supported");
#endif
    return EPROTONOSUPPORT;
  }
  fruid_board->area_len = board[index++] * FRUID_AREA_LEN_MULTIPLIER;
  fruid_board->lang_code = board[index++];

  fruid_board->chksum = board[fruid_board->area_len - 1];
  ret = verify_chksum((uint8_t *) board,
          fruid_board->area_len, fruid_board->chksum);

  if (ret) {
#ifdef DEBUG
    syslog(LOG_ERR, "fruid: board_area: chksum not verified.");
#endif
    return EBADF;
  }

  for (i = 0; i < 3; i++) {
    fruid_board->mfg_time[i] = board[index++];
  }

  fruid_board->mfg_time_str = calculate_time(fruid_board->mfg_time);
  if (fruid_board->mfg_time_str == NULL)
    return ENOMEM;

  fruid_board->mfg = _fruid_area_field_read(&board[index]);
  if (fruid_board->mfg == NULL)
    return ENOMEM;
  index += FIELD_LEN(board[index]) + 1;

  fruid_board->name = _fruid_area_field_read(&board[index]);
  if (fruid_board->name == NULL)
    return ENOMEM;
  index += FIELD_LEN(board[index]) + 1;

  fruid_board->serial = _fruid_area_field_read(&board[index]);
  if (fruid_board->serial == NULL)
    return ENOMEM;
  index += FIELD_LEN(board[index]) + 1;

  fruid_board->part = _fruid_area_field_read(&board[index]);
  if (fruid_board->part == NULL)
    return ENOMEM;
  index += FIELD_LEN(board[index]) + 1;

  fruid_board->fruid = _fruid_area_field_read(&board[index]);
  if (fruid_board->fruid == NULL)
    return ENOMEM;
  index += FIELD_LEN(board[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (board[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_board->custom1 = _fruid_area_field_read(&board[index]);
  if (fruid_board->custom1 == NULL)
    return ENOMEM;
  index += FIELD_LEN(board[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (board[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_board->custom2 = _fruid_area_field_read(&board[index]);
  if (fruid_board->custom2 == NULL)
    return ENOMEM;
  index += FIELD_LEN(board[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (board[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_board->custom3 = _fruid_area_field_read(&board[index]);
  if (fruid_board->custom3 == NULL)
    return ENOMEM;
  index += FIELD_LEN(board[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (board[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_board->custom4 = _fruid_area_field_read(&board[index]);
  if (fruid_board->custom4 == NULL)
    return ENOMEM;

  return 0;
}

/* Parse the Chassis area data */
int parse_fruid_area_chassis(uint8_t * chassis,
      fruid_area_chassis_t * fruid_chassis)
{
  int ret, index;

  index = 0;

  /* Reset the struct to zero */
  memset(fruid_chassis, 0, sizeof(fruid_area_chassis_t));

  /* Check if the format version is as per IPMI FRUID v1.0 format spec */
  fruid_chassis->format_ver = chassis[index++];
  if (fruid_chassis->format_ver != FRUID_FORMAT_VER) {
#ifdef DEBUG
    syslog(LOG_ERR, "fruid: chassis_area: format version not supported");
#endif
    return EPROTONOSUPPORT;
  }

  fruid_chassis->area_len = chassis[index++] * FRUID_AREA_LEN_MULTIPLIER;
  fruid_chassis->type = chassis[index++];

  fruid_chassis->chksum = chassis[fruid_chassis->area_len - 1];
  ret = verify_chksum((uint8_t *) chassis,
          fruid_chassis->area_len, fruid_chassis->chksum);
  if (ret) {
#ifdef DEBUG
    syslog(LOG_ERR, "fruid: chassis_area: chksum not verified.");
#endif
    return EBADF;
  }

  fruid_chassis->type_str = get_chassis_type(fruid_chassis->type);
  if (fruid_chassis->type_str == NULL)
    return ENOMSG;

  fruid_chassis->part = _fruid_area_field_read(&chassis[index]);
  if (fruid_chassis->part == NULL)
    return ENOMEM;
  index += FIELD_LEN(chassis[index]) + 1;

  fruid_chassis->serial = _fruid_area_field_read(&chassis[index]);
  if (fruid_chassis->serial == NULL)
    return ENOMEM;
  index += FIELD_LEN(chassis[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (chassis[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_chassis->custom1 = _fruid_area_field_read(&chassis[index]);
  if (fruid_chassis->custom1 == NULL)
    return ENOMEM;
  index += FIELD_LEN(chassis[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (chassis[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_chassis->custom2 = _fruid_area_field_read(&chassis[index]);
  if (fruid_chassis->custom2 == NULL)
    return ENOMEM;
  index += FIELD_LEN(chassis[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (chassis[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_chassis->custom3 = _fruid_area_field_read(&chassis[index]);
  if (fruid_chassis->custom3 == NULL)
    return ENOMEM;
  index += FIELD_LEN(chassis[index]) + 1;

  /* Check if this field was last and there is no more custom data */
  if (chassis[index] == NO_MORE_DATA_BYTE)
    return 0;
  fruid_chassis->custom4 = _fruid_area_field_read(&chassis[index]);
  if (fruid_chassis->custom4 == NULL)
    return ENOMEM;

  return 0;
}

/* Calculate the area offsets and populate the fruid_eeprom_t struct */
void set_fruid_eeprom_offsets(uint8_t * eeprom, fruid_header_t * header,
      fruid_eeprom_t * fruid_eeprom)
{
  fruid_eeprom->header = eeprom + 0x00;

  header->offset_area.chassis ? (fruid_eeprom->chassis = eeprom + \
    (header->offset_area.chassis * FRUID_OFFSET_MULTIPLIER)) : \
    (fruid_eeprom->chassis = NULL);

  header->offset_area.board ? (fruid_eeprom->board = eeprom + \
    (header->offset_area.board * FRUID_OFFSET_MULTIPLIER)) : \
    (fruid_eeprom->board = NULL);

  header->offset_area.product ? (fruid_eeprom->product = eeprom + \
    (header->offset_area.product * FRUID_OFFSET_MULTIPLIER)) : \
    (fruid_eeprom->product = NULL);

  header->offset_area.multirecord ? (fruid_eeprom->multirecord = eeprom + \
    (header->offset_area.multirecord * FRUID_OFFSET_MULTIPLIER)) : \
    (fruid_eeprom->multirecord = NULL);
}

/* Populate the common header struct */
int parse_fruid_header(uint8_t * eeprom, fruid_header_t * header)
{
  int ret;

  memcpy((uint8_t *)header, (uint8_t *)eeprom, sizeof(fruid_header_t));
  ret = verify_chksum((uint8_t *) header,
          sizeof(fruid_header_t), header->chksum);
  if (ret) {
#ifdef DEBUG
    syslog(LOG_ERR, "fruid: common_header: chksum not verified.");
#endif
    return EBADF;
  }

  return ret;
}

/* Parse the eeprom dump and populate the fruid info in struct */
int populate_fruid_info(fruid_eeprom_t * fruid_eeprom, fruid_info_t * fruid)
{
  int ret;

  /* Initial all the required fruid structures */
  fruid_area_chassis_t fruid_chassis;
  fruid_area_board_t fruid_board;
  fruid_area_product_t fruid_product;

  /* If Chassis area is present, parse and print it */
  if (fruid_eeprom->chassis) {
    ret = parse_fruid_area_chassis(fruid_eeprom->chassis, &fruid_chassis);
    if (!ret) {
      fruid->chassis.flag = 1;
      fruid->chassis.type_str = fruid_chassis.type_str;
      fruid->chassis.part = fruid_chassis.part;
      fruid->chassis.serial = fruid_chassis.serial;
      fruid->chassis.custom1 = fruid_chassis.custom1;
      fruid->chassis.custom2 = fruid_chassis.custom2;
      fruid->chassis.custom3 = fruid_chassis.custom3;
      fruid->chassis.custom4 = fruid_chassis.custom4;
    } else
      return ret;
  }

  /* If Board area is present, parse and print it */
  if (fruid_eeprom->board) {
    ret = parse_fruid_area_board(fruid_eeprom->board, &fruid_board);
    if (!ret) {
      fruid->board.flag = 1;
      fruid->board.mfg_time_str = fruid_board.mfg_time_str;
      fruid->board.mfg = fruid_board.mfg;
      fruid->board.name = fruid_board.name;
      fruid->board.serial = fruid_board.serial;
      fruid->board.part = fruid_board.part;
      fruid->board.fruid = fruid_board.fruid;
      fruid->board.custom1 = fruid_board.custom1;
      fruid->board.custom2 = fruid_board.custom2;
      fruid->board.custom3 = fruid_board.custom3;
      fruid->board.custom4 = fruid_board.custom4;
    } else
      return ret;
  }

  /* If Product area is present, parse and print it */
  if (fruid_eeprom->product) {
    ret = parse_fruid_area_product(fruid_eeprom->product, &fruid_product);
    if (!ret) {
      fruid->product.flag = 1;
      fruid->product.mfg = fruid_product.mfg;
      fruid->product.name = fruid_product.name;
      fruid->product.part = fruid_product.part;
      fruid->product.version = fruid_product.version;
      fruid->product.serial = fruid_product.serial;
      fruid->product.asset_tag = fruid_product.asset_tag;
      fruid->product.fruid = fruid_product.fruid;
      fruid->product.custom1 = fruid_product.custom1;
      fruid->product.custom2 = fruid_product.custom2;
      fruid->product.custom3 = fruid_product.custom3;
      fruid->product.custom4 = fruid_product.custom4;
    } else
      return ret;
  }

  return 0;
}

/*
 * fruid_parse - To parse the bin file (eeprom) and populate
 *               the fruid information in the struct
 * @bin       : Eeprom binary file
 * @fruid     : ptr to the struct that holds the fruid information
 *
 * returns 0 on success
 * returns non-zero errno value on error
 */
int fruid_parse(const char * bin, fruid_info_t * fruid)
{
  int fruid_len, ret;
  FILE *fruid_fd;
  uint8_t * eeprom;

  /* Reset parser return value */
  ret = 0;

  /* Open the FRUID binary file */
  fruid_fd = fopen(bin, "rb");
  if (!fruid_fd) {
#ifdef DEBUG
    syslog(LOG_ERR, "fruid: unable to open the file");
#endif
    return ENOENT;
  }

  /* Get the size of the binary file */
  fseek(fruid_fd, 0, SEEK_END);
  fruid_len = (uint32_t) ftell(fruid_fd);

  fseek(fruid_fd, 0, SEEK_SET);

  eeprom = (uint8_t *) malloc(fruid_len);
  if (!eeprom) {
#ifdef DEBUG
    syslog(LOG_WARNING, "fruid: malloc: memory allocation failed\n");
#endif
    return ENOMEM;
  }

  /* Read the binary file */
  fread(eeprom, sizeof(uint8_t), fruid_len, fruid_fd);

  /* Close the FRUID binary file */
  fclose(fruid_fd);

  /* Parse eeprom dump*/
  ret = fruid_parse_eeprom(eeprom, fruid_len, fruid);

  /* Free the eeprom malloced memory */
  free(eeprom);
  return ret;
}

/* Populate the fruid from eeprom dump*/
int fruid_parse_eeprom(const uint8_t * eeprom, int eeprom_len, fruid_info_t * fruid)
{
  int ret = 0;

  /* Initial all the required fruid structures */
  fruid_header_t fruid_header;
  fruid_eeprom_t fruid_eeprom;

  memset(&fruid_header, 0, sizeof(fruid_header_t));
  memset(&fruid_eeprom, 0, sizeof(fruid_eeprom_t));

  /* Parse the common header data */
  ret = parse_fruid_header(eeprom, &fruid_header);
  if (ret) {
    return ret;
  }

  /* Calculate all the area offsets */
  set_fruid_eeprom_offsets(eeprom, &fruid_header, &fruid_eeprom);

  init_fruid_info(fruid);
  /* Parse the eeprom and populate the fruid information */
  ret = populate_fruid_info(&fruid_eeprom, fruid);
  if (ret) {
    /* Free the malloced memory for the fruid information */
    free_fruid_info(fruid);
  }

  return ret;
}

/* Get fru content from eeprom dump*/
static 
int get_fruid_content(const uint8_t * eeprom, int eeprom_len, fruid_info_t * fruid, fruid_header_t * fruid_header, fruid_eeprom_t * fruid_eeprom, fruid_area_chassis_t * fruid_chassis,
                      fruid_area_board_t * fruid_board, fruid_area_product_t * fruid_product)
{
  int ret = 0;

  /* Parse the common header data */
  ret = parse_fruid_header(eeprom, fruid_header);
  if (ret) {
    return ret;
  }

  /* Calculate all the area offsets */
  set_fruid_eeprom_offsets(eeprom, fruid_header, fruid_eeprom);

  init_fruid_info(fruid);

  /* If Chassis area is present, parse it */
  if (fruid_eeprom->chassis) {
    ret = parse_fruid_area_chassis(fruid_eeprom->chassis, fruid_chassis);
    if (!ret) {
      fruid->chassis.flag = 1;
      fruid->chassis.type_str = fruid_chassis->type_str;
      fruid->chassis.part = fruid_chassis->part;
      fruid->chassis.serial = fruid_chassis->serial;
      fruid->chassis.custom1 = fruid_chassis->custom1;
      fruid->chassis.custom2 = fruid_chassis->custom2;
      fruid->chassis.custom3 = fruid_chassis->custom3;
      fruid->chassis.custom4 = fruid_chassis->custom4;
    } else {
      free_fruid_info(fruid);
      return ret;
    }
  }

  /* If Board area is present, parse it */
  if (fruid_eeprom->board) {
    ret = parse_fruid_area_board(fruid_eeprom->board, fruid_board);
    if (!ret) {
      fruid->board.flag = 1;
      fruid->board.mfg_time_str = fruid_board->mfg_time_str;
      fruid->board.mfg = fruid_board->mfg;
      fruid->board.name = fruid_board->name;
      fruid->board.serial = fruid_board->serial;
      fruid->board.part = fruid_board->part;
      fruid->board.fruid = fruid_board->fruid;
      fruid->board.custom1 = fruid_board->custom1;
      fruid->board.custom2 = fruid_board->custom2;
      fruid->board.custom3 = fruid_board->custom3;
      fruid->board.custom4 = fruid_board->custom4;
    } else {
      free_fruid_info(fruid);
      return ret;
    }
  }

  /* If Product area is present, parse it */
  if (fruid_eeprom->product) {
    ret = parse_fruid_area_product(fruid_eeprom->product, fruid_product);
    if (!ret) {
      fruid->product.flag = 1;
      fruid->product.mfg = fruid_product->mfg;
      fruid->product.name = fruid_product->name;
      fruid->product.part = fruid_product->part;
      fruid->product.version = fruid_product->version;
      fruid->product.serial = fruid_product->serial;
      fruid->product.asset_tag = fruid_product->asset_tag;
      fruid->product.fruid = fruid_product->fruid;
      fruid->product.custom1 = fruid_product->custom1;
      fruid->product.custom2 = fruid_product->custom2;
      fruid->product.custom3 = fruid_product->custom3;
      fruid->product.custom4 = fruid_product->custom4;
    } else {
      free_fruid_info(fruid);
      return ret;
    }
  }

  return ret;
}

static
void copy_chassis_info(fruid_field_t *fruid_field, fruid_eeprom_t *fruid_eeprom, fruid_area_chassis_t *fruid_chassis, int *order) {
  int index = 0;
  uint8_t *chassis;
  int i = *order;

  /* If Chassis area is present, copy it */
  if (fruid_eeprom->chassis) {
    index = 3;                                                      // Byte0 : Chassis Info Area Format Version, Byte1 : Chassis Info Area Length, Byte2 : Chassis Type
    chassis = fruid_eeprom->chassis;
    fruid_field[i].field = fruid_field_chassis_opt[i - *order];     // Byte3 : Chassis Part Number
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_chassis->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
    fruid_field[i].offset = chassis + index;
    fruid_field[i].type_length = FIELD_LEN(chassis[index]);
    index += FIELD_LEN(chassis[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_chassis_opt[i - *order];     // Chassis Serial Number
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_chassis->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
    fruid_field[i].offset = chassis + index;
    fruid_field[i].type_length = FIELD_LEN(chassis[index]);
    index += FIELD_LEN(chassis[index]) + 1;
    i++;

    if (chassis[index] == NO_MORE_DATA_BYTE) {                      // Chassis Custom Data 1
      fruid_field[i].field = fruid_field_chassis_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_chassis_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_chassis->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
      fruid_field[i].offset = chassis + index;
      fruid_field[i].type_length = FIELD_LEN(chassis[index]);
      index += FIELD_LEN(chassis[index]) + 1;
      i++;
    }
    if (chassis[index] == NO_MORE_DATA_BYTE) {                      // Chassis Custom Data 2
      fruid_field[i].field = fruid_field_chassis_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_chassis_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_chassis->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
      fruid_field[i].offset = chassis + index;
      fruid_field[i].type_length = FIELD_LEN(chassis[index]);
      index += FIELD_LEN(chassis[index]) + 1;
      i++;
    }
    if (chassis[index] == NO_MORE_DATA_BYTE) {                      // Chassis Custom Data 3
      fruid_field[i].field = fruid_field_chassis_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_chassis_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_chassis->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
      fruid_field[i].offset = chassis + index;
      fruid_field[i].type_length = FIELD_LEN(chassis[index]);
      index += FIELD_LEN(chassis[index]) + 1;
      i++;
    }
    if (chassis[index] == NO_MORE_DATA_BYTE) {                      // Chassis Custom Data 4
      fruid_field[i].field = fruid_field_chassis_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_chassis_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_chassis->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
      fruid_field[i].offset = chassis + index;
      fruid_field[i].type_length = FIELD_LEN(chassis[index]);
      index += FIELD_LEN(chassis[index]) + 1;
      i++;
    }
  } else {
    for(i = 0; i < field_chassis_opt_size; i++) {
      fruid_field[i].field = fruid_field_chassis_opt[i - *order];
      fruid_field[i].flag = 0;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_CHASSIS;
    }
  }

  *order = i;
  return;
}

static
void copy_board_info(fruid_field_t *fruid_field, fruid_eeprom_t *fruid_eeprom, fruid_area_board_t *fruid_board, int *order) {
  int index = 0;
  uint8_t *board;
  int i = *order;

  /* If Board area is present, copy it */
  if (fruid_eeprom->board) {
    index = 3;                                                  // Byte0 : Board Area Format Version, Byte1 : Board Area Length, Byte2 : Language Code
    board = fruid_eeprom->board;                          
    fruid_field[i].field = fruid_field_board_opt[i - *order];   // Byte3 : Mfg. Date/Time
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_board->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
    fruid_field[i].offset = board + index;
    fruid_field[i].type_length = 3;                             // 3 bytes for Mfg. Date/Time
    index += fruid_field[i].type_length;
    i++;

    fruid_field[i].field = fruid_field_board_opt[i - *order];   // Board Manufacturer
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_board->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
    fruid_field[i].offset = board + index;
    fruid_field[i].type_length = FIELD_LEN(board[index]);
    index += FIELD_LEN(board[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_board_opt[i - *order];   // Board Product Name
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_board->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
    fruid_field[i].offset = board + index;
    fruid_field[i].type_length = FIELD_LEN(board[index]);
    index += FIELD_LEN(board[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_board_opt[i - *order];   // Board Serial Number
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_board->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
    fruid_field[i].offset = board + index;
    fruid_field[i].type_length = FIELD_LEN(board[index]);
    index += FIELD_LEN(board[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_board_opt[i - *order];   // Board Part Number
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_board->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
    fruid_field[i].offset = board + index;
    fruid_field[i].type_length = FIELD_LEN(board[index]);
    index += FIELD_LEN(board[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_board_opt[i - *order];   // FRU File ID
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_board->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
    fruid_field[i].offset = board + index;
    fruid_field[i].type_length = FIELD_LEN(board[index]);
    index += FIELD_LEN(board[index]) + 1;
    i++;

    if (board[index] == NO_MORE_DATA_BYTE) {                    // Board Custom Data 1
      fruid_field[i].field = fruid_field_board_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_board_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_board->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
      fruid_field[i].offset = board + index;
      fruid_field[i].type_length = FIELD_LEN(board[index]);
      index += FIELD_LEN(board[index]) + 1;
      i++;
    }
    if (board[index] == NO_MORE_DATA_BYTE) {                    // Board Custom Data 2
      fruid_field[i].field = fruid_field_board_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_board_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_board->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
      fruid_field[i].offset = board + index;
      fruid_field[i].type_length = FIELD_LEN(board[index]);
      index += FIELD_LEN(board[index]) + 1;
      i++;
    }
    if (board[index] == NO_MORE_DATA_BYTE) {                    // Board Custom Data 3
      fruid_field[i].field = fruid_field_board_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_board_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_board->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
      fruid_field[i].offset = board + index;
      fruid_field[i].type_length = FIELD_LEN(board[index]);
      index += FIELD_LEN(board[index]) + 1;
      i++;
    }
    if (board[index] == NO_MORE_DATA_BYTE) {                    // Board Custom Data 4
      fruid_field[i].field = fruid_field_board_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_board_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_board->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
      fruid_field[i].offset = board + index;
      fruid_field[i].type_length = FIELD_LEN(board[index]);
      index += FIELD_LEN(board[index]) + 1;
      i++;
    }
  } else {
    for(i = 0; i < field_board_opt_size; i++) {
      fruid_field[i].field = fruid_field_board_opt[i - *order];
      fruid_field[i].flag = 0;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_BOARD;
    }
  }

  *order = i;
  return;
}

static
void copy_product_info(fruid_field_t *fruid_field, fruid_eeprom_t *fruid_eeprom, fruid_area_product_t *fruid_product, int *order) {
  int index = 0;
  uint8_t *product;
  int i = *order;

  /* If Product area is present, copy it */
  if (fruid_eeprom->product) {
    index = 3;                                                   // Byte0 : Product Area Format Version, Byte1 : Product Area Length, Byte2 : Language Code
    product = fruid_eeprom->product;
    fruid_field[i].field = fruid_field_product_opt[i - *order];  // Byte3 : Manufacturer Name
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_product->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
    fruid_field[i].offset = product + index;
    fruid_field[i].type_length = FIELD_LEN(product[index]);                     
    index += FIELD_LEN(product[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_product_opt[i - *order];  // Product Name
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_product->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
    fruid_field[i].offset = product + index;
    fruid_field[i].type_length = FIELD_LEN(product[index]);
    index += FIELD_LEN(product[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_product_opt[i - *order];  // Product Part/Model Number
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_product->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
    fruid_field[i].offset = product + index;
    fruid_field[i].type_length = FIELD_LEN(product[index]);
    index += FIELD_LEN(product[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_product_opt[i - *order];  // Product Version
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_product->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
    fruid_field[i].offset = product + index;
    fruid_field[i].type_length = FIELD_LEN(product[index]);
    index += FIELD_LEN(product[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_product_opt[i - *order];  // Product Serial Number
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_product->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
    fruid_field[i].offset = product + index;
    fruid_field[i].type_length = FIELD_LEN(product[index]);
    index += FIELD_LEN(product[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_product_opt[i - *order];  // Asset Tag
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_product->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
    fruid_field[i].offset = product + index;
    fruid_field[i].type_length = FIELD_LEN(product[index]);
    index += FIELD_LEN(product[index]) + 1;
    i++;

    fruid_field[i].field = fruid_field_product_opt[i - *order];  // FRU File ID
    fruid_field[i].flag = 1;
    fruid_field[i].area_length = fruid_product->area_len;
    fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
    fruid_field[i].offset = product + index;
    fruid_field[i].type_length = FIELD_LEN(product[index]);
    index += FIELD_LEN(product[index]) + 1;
    i++;

    if (product[index] == NO_MORE_DATA_BYTE) {                   // Product Custom Data 1
      fruid_field[i].field = fruid_field_product_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_product_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_product->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
      fruid_field[i].offset = product + index;
      fruid_field[i].type_length = FIELD_LEN(product[index]);
      index += FIELD_LEN(product[index]) + 1;
      i++;
    }
    if (product[index] == NO_MORE_DATA_BYTE) {                   // Product Custom Data 2
      fruid_field[i].field = fruid_field_product_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_product_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_product->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
      fruid_field[i].offset = product + index;
      fruid_field[i].type_length = FIELD_LEN(product[index]);
      index += FIELD_LEN(product[index]) + 1;
      i++;
    }
    if (product[index] == NO_MORE_DATA_BYTE) {                   // Product Custom Data 3
      fruid_field[i].field = fruid_field_product_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_product_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_product->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
      fruid_field[i].offset = product + index;
      fruid_field[i].type_length = FIELD_LEN(product[index]);
      index += FIELD_LEN(product[index]) + 1;
      i++;
    }
    if (product[index] == NO_MORE_DATA_BYTE) {                   // Product Custom Data 4
      fruid_field[i].field = fruid_field_product_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
      fruid_field[i].type_length = 0;
      i++;
    } else {
      fruid_field[i].field = fruid_field_product_opt[i - *order];
      fruid_field[i].flag = 1;
      fruid_field[i].area_length = fruid_product->area_len;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT;
      fruid_field[i].offset = product + index;
      fruid_field[i].type_length = FIELD_LEN(product[index]);
      index += FIELD_LEN(product[index]) + 1;
      i++;
    }
  } else {
    for(i = 0; i < field_product_opt_size; i++) {
      fruid_field[i].field = fruid_field_product_opt[i - *order];
      fruid_field[i].flag = 0;
      fruid_field[i].area_type = FRUID_OFFSET_AREA_PRODUCT; 
    }
  }

  *order = i;
  return;
}

static
void copy_fru_info(fruid_field_t *fruid_field, fruid_eeprom_t * fruid_eeprom, fruid_area_chassis_t * fruid_chassis,
                   fruid_area_board_t * fruid_board, fruid_area_product_t * fruid_product)
{
  int order = 0;

  copy_chassis_info(fruid_field, fruid_eeprom, fruid_chassis, &order);
  copy_board_info(fruid_field, fruid_eeprom, fruid_board, &order);
  copy_product_info(fruid_field, fruid_eeprom, fruid_product, &order);

  return; 
}

static 
char *extract_content(const char *content, char *tmp_str) {
  int i = 0, j = 0;
  
  while(content[i] != '\0') {
    if(content[i] != '\"') {
      tmp_str[j++] = content[i];  
    }
    i++;
  }
  tmp_str[j] = '\0';

  return tmp_str;
}

static 
int calculate_chksum(fruid_field_t * fruid_field, fruid_eeprom_t * fruid_eeprom) {
  uint8_t *start_offset;
  int i;
  uint8_t chksum = 0;
  int ret;

  switch(fruid_field->area_type) {
    case FRUID_OFFSET_AREA_CHASSIS:
      start_offset = fruid_eeprom->chassis;
      break;
    case FRUID_OFFSET_AREA_BOARD:
      start_offset = fruid_eeprom->board;
      break;
    case FRUID_OFFSET_AREA_PRODUCT:
      start_offset = fruid_eeprom->product;
      break;
    default:
      return -1;
  }

  for (i = 0; i < fruid_field->area_length - 1; i++) {
    chksum += start_offset[i];
  }

  /* Zero checksum calculation */
  chksum = ~(chksum) + 1;

  /* Update new checksum */
  start_offset[fruid_field->area_length - 1] = chksum; 

  ret = verify_chksum((uint8_t *) start_offset, fruid_field->area_length, start_offset[fruid_field->area_length - 1]);
      
  return ret;
}

static
int set_mfg_time(fruid_field_t * fruid_field, char * set_time) {
  uint32_t time_value; 
  uint32_t datetime;
  time_t ipmi_time;
  char *ptr = NULL;

  if (!strcmp(set_time, "")) {   //Invalid time setting
    return -1;
  }

  strtol(set_time, &ptr, 10);

  if(strcmp(ptr, "")) {       //Invalid time setting
    return -1;
  }

  time_value = (uint32_t)atoi(set_time);
   
  ipmi_time = UNIX_TIMESTAMP_1996;

  datetime = (time_value - ipmi_time) / 60;

  *(fruid_field->offset) = (datetime & 0xFF);
  *(fruid_field->offset + 1) = ((datetime >> 8) & 0xFF);
  *(fruid_field->offset + 2) = ((datetime >> 16) & 0xFF);

  return 0;
}

int fruid_modify(const char * cur_bin, const char * new_bin, const char * field, const char * content)
{
  int fruid_len, ret;
  FILE *fruid_fd;
  uint8_t *eeprom;
  int total_field_opt_size = field_chassis_opt_size + field_board_opt_size + field_product_opt_size;
  fruid_info_t fruid;
  fruid_field_t fruid_field[total_field_opt_size];
  int index;
  int target = -1;
  char tmp_str[256];
  char *tmp_content = NULL;
  char *area_str = NULL;
  int content_len;
  
  /* Initial all the required fruid structures */
  fruid_header_t fruid_header;
  fruid_eeprom_t fruid_eeprom;
  fruid_area_chassis_t fruid_chassis;
  fruid_area_board_t fruid_board;
  fruid_area_product_t fruid_product;

  /* Reset parser return value */
  ret = 0;

  /* Open the FRUID binary file */
  fruid_fd = fopen(cur_bin, "rb");
  if (!fruid_fd) {
#ifdef DEBUG
    syslog(LOG_ERR, "%s: unable to open the file %s", __func__, cur_bin);
#endif
    return ENOENT;
  }

  /* Get the size of the binary file */
  fseek(fruid_fd, 0, SEEK_END);
  fruid_len = (uint32_t) ftell(fruid_fd);

  fseek(fruid_fd, 0, SEEK_SET);

  eeprom = (uint8_t *) malloc(fruid_len);
  if (!eeprom) {
#ifdef DEBUG
    syslog(LOG_WARNING, "%s: malloc: memory allocation failed", __func__);
#endif
    return ENOMEM;
  }

  /* Read the binary file */
  fread(eeprom, sizeof(uint8_t), fruid_len, fruid_fd);

  /* Close the FRUID binary file */
  fclose(fruid_fd);

  /* Get current fru content*/
  memset(&fruid_header, 0, sizeof(fruid_header_t));
  memset(&fruid_eeprom, 0, sizeof(fruid_eeprom_t));

  ret = get_fruid_content(eeprom, fruid_len, &fruid, &fruid_header, &fruid_eeprom, &fruid_chassis, &fruid_board, &fruid_product);
  if (ret) {
    return ret;
  }

  copy_fru_info(fruid_field, &fruid_eeprom, &fruid_chassis, &fruid_board, &fruid_product);

  for(index = 0; index < total_field_opt_size; index++) {
    if (!strcmp(field, fruid_field[index].field)) {
      target = index;
      break;
    }
  }
  if (target == -1) {
    printf("Parameter \"%s\" is invalid!\n", field);
    ret = -1;
    goto error_exit;
  }

  switch(fruid_field[target].area_type) {
    case FRUID_OFFSET_AREA_CHASSIS:
      area_str = "Chassis";
      break;
    case FRUID_OFFSET_AREA_BOARD:
      area_str = "Board";
      break;
    case FRUID_OFFSET_AREA_PRODUCT:
      area_str = "Product";
      break;
    default:
      area_str = "Unknown";
      break;
  }

  if (fruid_field[target].flag == 0) {           //Check if area is present   
    printf("Area %s is disabled!\n", area_str);
    ret = -1;
    goto error_exit;
  }

  tmp_content = extract_content(content, tmp_str);
  content_len = strlen(tmp_content);

  if(!strcmp(fruid_field[target].field, "--BMD")) {
    if(set_mfg_time(&fruid_field[target], tmp_content) < 0) {
      ret = -1;
      goto error_exit;
    }
  } else {
    if (fruid_field[target].type_length == 0) {    //Check if field is not used
      printf("Field %s is not used!\n", fruid_field[target].field);
      ret = -1;
      goto error_exit;
    }

    if(content_len > fruid_field[target].type_length) {  //Check if input length of specified field exceeds predefined field length
      printf("Exceed field length of %s!\n", fruid_field[target].field);
      ret = -1;
      goto error_exit;
    }

    memset(fruid_field[target].offset + 1, 0, fruid_field[target].type_length);
    memcpy(fruid_field[target].offset + 1, tmp_content, content_len);
  }

  ret = calculate_chksum(&fruid_field[target], &fruid_eeprom);
  if (ret < 0) {
    printf("Update checksum fail!\n");
    goto error_exit;
  }

  /* Open the FRUID binary file */
  fruid_fd = fopen(new_bin, "wb");
  if (!fruid_fd) {
#ifdef DEBUG
    syslog(LOG_ERR, "%s: unable to open the file %s", __func__, new_bin);
#endif
    return ENOENT;
  }

  /* Read the binary file */
  fwrite(eeprom, sizeof(uint8_t), fruid_len, fruid_fd);

  /* Close the FRUID binary file */
  fclose(fruid_fd);
 
error_exit:
  /* Free the eeprom malloced memory */
  free(eeprom);
  /* Free the malloced memory for the fruid information */
  free_fruid_info(&fruid);
  return ret;
}
