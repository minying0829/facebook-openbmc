/*
 *
 * Copyright 2015-present Facebook. All Rights Reserved.
 *
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

#ifndef __BIC_M2_FWUPDATE_H__
#define __BIC_M2_FWUPDATE_H__

#ifdef __cplusplus
extern "C" {
#endif

int update_bic_m2_fw(uint8_t slot_id, uint8_t comp, char *image, uint8_t intf, uint8_t force, uint8_t type);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __BIC_PCIESW_FWUPDATE_H__ */