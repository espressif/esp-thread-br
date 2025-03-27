#!/bin/bash

# Define the commands in a list
commands=(
    "components/esp_rcp_update build_examples_result.txt"
    "components/esp_ot_cli_extension build_examples_result.txt"
    "components/esp_ot_cli_extension build_idf_otbr_example_result.txt"
    "components/esp_ot_cli_extension build_idf_otbr_autostart_example_result.txt"
    "components/esp_ot_cli_extension build_idf_otcli_example_result.txt"
)

# Loop through the list and run the commands
for cmd in "${commands[@]}"; do
    python3 check_idf_example_build_results.py $cmd
    if [ $? -eq 1 ]; then
        echo "Error: The check for $cmd failed."
        exit 1
    fi
done

# Exit with 0 if all commands succeed
exit 0
