# Copyright 2021-present Facebook. All Rights Reserved.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA
inherit systemd
inherit meson

SUMMARY = "Rackmon Functionality"
DESCRIPTION = "Rackmon Functionality"
SECTION = "base"
PR = "r1"
LICENSE = "GPLv2"
# The license GPL-2.0 was removed in Hardknott.
# Use GPL-2.0-only instead.
def lic_file_name(d):
    distro = d.getVar('DISTRO_CODENAME', True)
    if distro in [ 'rocko', 'zeus', 'dunfell' ]:
        return "GPL-2.0;md5=801f80980d171dd6425610833a22dbe6"

    return "GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

LIC_FILES_CHKSUM = "\
    file://${COREBASE}/meta/files/common-licenses/${@lic_file_name(d)} \
    "

DEPENDS:append = " update-rc.d-native"

LDFLAGS += "-llog -lmisc-utils"
DEPENDS += "liblog libmisc-utils nlohmann-json cli11"
RDEPENDS:${PN} = "liblog libmisc-utils python3-core bash"

def get_profile_flag(d):
  prof_enabled = d.getVar("RACKMON_PROFILING", False)
  if prof_enabled:
    return "-DPROFILING"
  return ""

CFLAGS += "${@get_profile_flag(d)}"


SRC_URI = "file://meson.build \
           file://rackmond.service \
           file://run-rackmond.sh \
           file://setup-rackmond.sh \
           file://dev.cpp \
           file://dev.hpp \
           file://modbus_cmds.cpp \
           file://modbus_cmds.hpp \
           file://modbus.cpp \
           file://modbus.hpp \
           file://msg.cpp \
           file://msg.hpp \
           file://uart.cpp \
           file://uart.hpp \
           file://regmap.cpp \
           file://regmap.hpp \
           file://modbus_device.cpp \
           file://modbus_device.hpp \
           file://rackmon.cpp \
           file://rackmon.hpp \
           file://rackmon_sock.cpp \
           file://rackmon_svc_unix.hpp \
           file://rackmon_svc_unix.cpp \
           file://rackmon_cli_unix.cpp \
          "
# Configuration files
SRC_URI += "file://rackmon.conf \
            file://rackmon.d/orv2_psu.json \
           "

S = "${WORKDIR}"

install_systemd() {
    install -d ${D}${systemd_system_unitdir}

    install -m 0644 rackmond.service ${D}${systemd_system_unitdir}
}

install_sysv() {
    install -d ${D}${sysconfdir}/init.d
    install -d ${D}${sysconfdir}/rcS.d
    install -d ${D}${sysconfdir}/sv
    install -d ${D}${sysconfdir}/sv/rackmond
    install -m 755 ${S}/run-rackmond.sh ${D}${sysconfdir}/sv/rackmond/run
    install -m 755 ${S}/setup-rackmond.sh ${D}${sysconfdir}/init.d/rackmond
    update-rc.d -r ${D} rackmond start 95 2 3 4 5  .
}

do_install:append() {
    if ${@bb.utils.contains('DISTRO_FEATURES', 'systemd', 'true', 'false', d)}; then
        install_systemd
    else
        install_sysv
    fi
    install -d ${D}${sysconfdir}/rackmon.d
    install -m 644 ${S}/rackmon.conf ${D}${sysconfdir}/rackmon.conf
    for f in ${S}/rackmon.d/*; do
      install ${f} ${D}${sysconfdir}/rackmon.d/
    done
}

FILES:${PN} = "${prefix}/local/bin ${sysconfdir} "

FILES:${PN} += "${@bb.utils.contains('DISTRO_FEATURES', 'systemd', '${systemd_system_unitdir}', '', d)}"

SYSTEMD_SERVICE:${PN} = "rackmond.service"