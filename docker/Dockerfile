ARG ESP_MATTER_VERSION
FROM espressif/esp-matter:release-${ESP_MATTER_VERSION}
ADD 0001-Add-NanoC6-support.patch /opt/espressif/esp-matter/
RUN cd /opt/espressif/esp-matter; patch --unified -p1 < 0001-Add-NanoC6-support.patch