################################################################################
#
# owlcaros-ui
#
################################################################################

OWLCAROS_UI_VERSION = 0.1.0
OWLCAROS_UI_SITE = $(BR2_EXTERNAL_OWLCAROS_PATH)/package/owlcaros-ui/src
OWLCAROS_UI_SITE_METHOD = local
OWLCAROS_UI_LICENSE = MIT
OWLCAROS_UI_LICENSE_FILES = LICENSE

define OWLCAROS_UI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define OWLCAROS_UI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/owlcaros-ui $(TARGET_DIR)/usr/bin/owlcaros-ui
	mkdir -p $(TARGET_DIR)/usr/share/owlcaros/media
	cp -f $(@D)/media/*.wav $(TARGET_DIR)/usr/share/owlcaros/media/ 2>/dev/null || true
endef

$(eval $(generic-package))
