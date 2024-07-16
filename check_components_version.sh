#!/bin/sh

export ESP_EXTCMD_COMPONENTS_PATH="components/esp_ot_cli_extension"

if git diff --name-only HEAD~1 | grep -q "${ESP_EXTCMD_COMPONENTS_PATH}/idf_component.yml"; then
    OLD_VERSION=$(git show HEAD~1:${ESP_EXTCMD_COMPONENTS_PATH}/idf_component.yml | grep -m 1 "^version: " | awk '{print $2}' | tr -d '"')
    NEW_VERSION=$(head -n 1 ${ESP_EXTCMD_COMPONENTS_PATH}/idf_component.yml | grep "^version: " | awk '{print $2}' | tr -d '"')
    if [ -z "$OLD_VERSION" ] || [ -z "$NEW_VERSION" ]; then
        echo "Error: version not found or incorrect format in ${ESP_EXTCMD_COMPONENTS_PATH}/idf_component.yml"
        exit 1
    fi
    if [ "$OLD_VERSION" != "$NEW_VERSION" ]; then
        OLD_MAJOR=$(echo $OLD_VERSION | cut -d. -f1)
        OLD_MINOR=$(echo $OLD_VERSION | cut -d. -f2)
        NEW_MAJOR=$(echo $NEW_VERSION | cut -d. -f1)
        NEW_MINOR=$(echo $NEW_VERSION | cut -d. -f2)
        if [ "$OLD_MAJOR" != "$NEW_MAJOR" ] || [ "$OLD_MINOR" != "$NEW_MINOR" ]; then
            echo "ALLOW_FAILURE=true"
        else
            echo "ALLOW_FAILURE=false"
        fi
    else
        echo "ALLOW_FAILURE=false"
    fi
else
    echo "ALLOW_FAILURE=false"
fi
