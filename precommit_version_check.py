import sys
import tools.ci.version_check_tools as vct
component_list = ["esp_ot_cli_extension", "esp_rcp_update"]
example_list = ["basic_thread_border_router"]

for component_name in component_list:
    print(f"Checking {component_name}")
    main_version = vct.get_component_version_from_git(component_name, "origin/main")
    current_version = vct.get_component_version_from_git(component_name)
    if not main_version or not current_version:
        sys.exit(1)
    if not vct.check_version_increment(main_version, current_version, True):
        sys.exit(1)
    for example_name in example_list:
        yaml_path = vct.get_path_of_yaml(example_name)
        example_version = vct.get_example_component_version_from_git(component_name, example_name)
        if not example_version:
            sys.exit(1)
        example_version_process = vct.process_version(example_version)
        if not example_version_process:
            sys.exit(1)
        if not vct.check_version_increment(example_version_process, current_version, False):
            print(f"{component_name} version of {example_name} is invalid, use {example_version}, latest is {current_version}")
            sys.exit(1)


print("Version check passed successfully.")
