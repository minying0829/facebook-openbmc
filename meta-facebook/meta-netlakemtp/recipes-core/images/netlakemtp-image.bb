# Copyright 2020-present Facebook. All Rights Reserved.

require recipes-core/images/fbobmc-image-meta.inc
require netlakemtp-image-layout.inc

require recipes-core/images/fb-openbmc-image.bb

# Include modules in rootfs
IMAGE_INSTALL += " \
  packagegroup-openbmc-base \
  packagegroup-openbmc-net \
  packagegroup-openbmc-python3 \
  packagegroup-openbmc-rest3 \
  gpiod \
  setup-gpio \
  sensor-util \
  sensor-mon \
  healthd \
  cfg-util \
  ipmid \
  fruid \
  front-paneld \
  fw-util \
  jbi \
  log-util \
  "
