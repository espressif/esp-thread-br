# tools/version_check_tools.py

import subprocess
import os
import re
import yaml

# Get the absolute path of the yml file
# The input parameter is the example name or component name
def get_path_of_yaml(local_name):
    idf_path = os.environ.get("IDF_PATH")
    esp_thread_br_path = os.environ.get("ESP_THREAD_BR_PATH")
    search_paths = []
    if idf_path:
        search_paths.append(os.path.join(idf_path, "examples", "openthread", local_name, "main", "idf_component.yml"))
    if esp_thread_br_path:
        search_paths.append(os.path.join(esp_thread_br_path, "examples", local_name, "main", "idf_component.yml"))
        search_paths.append(os.path.join(esp_thread_br_path, "components", local_name, "idf_component.yml"))
    for path in search_paths:
        if os.path.exists(path):
            return path
    print(f"Failed to get yaml path of {local_name} from {idf_path} or {esp_thread_br_path}")
    return None

# Get the absolute path of the component
# The input parameter is the component name
def get_path_of_component(local_name):
    esp_thread_br_path = os.environ.get("ESP_THREAD_BR_PATH")
    search_paths = []
    if esp_thread_br_path:
        search_paths.append(os.path.join(esp_thread_br_path, "components", local_name))
    for path in search_paths:
        if os.path.exists(path):
            return path
    print(f"Failed to get component path of {local_name} from {esp_thread_br_path}")
    return None

# Get the relative path of B to A
# The input parameter is the absolute path of A and B
def get_relative_path(file_a, file_b):
    try:
        relative_path = os.path.relpath(file_b, start=os.path.dirname(file_a))
        return relative_path
    except Exception as e:
        print(f"Failed to get the relative path of {file_b} to {file_a} ")
        return None

# Get the component's version number
# Input 1: Absolute path of the component's idf_component.yml
# Input 2: Branch or commit name string, if not provided, the current branch is used by default
def get_component_version_from_git(component_name, branch=None):
    file_path = get_path_of_yaml(component_name)
    if not file_path:
        print(f"Failed to get the yaml path of {component_name} for branch {branch}")
        return None
    try:
        if branch is None:
            with open(file_path, 'r') as f:
                first_line = f.readline().strip()
            if first_line.startswith('version:'):
                version = first_line.split(":")[1].strip().strip('"')
                return version
            else:
                print(f"Failed to get the version of {component_name} for current branch")
                return None
        else:
            repo_root = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip()
            relative_path = os.path.relpath(file_path, repo_root)
            result = subprocess.run(['git', 'show', f'{branch}:{relative_path}'], capture_output=True, text=True)
            first_line = result.stdout.strip().split('\n')[0]
            if first_line.startswith('version:'):
                version = first_line.split(":")[1].strip().strip('"')
                return version
            else:
                print(f"Failed to get the version of {component_name} for branch {branch}")
                return None
    except Exception as e:
        return None

# Get the version number of a component used by an example
# Input 1: Component name (e.g., esp_rcp_update)
# Input 2: Absolute path of the example's idf_component.yml
# Input 3: Branch or commit name string, if not provided, the current branch is used by default
def get_example_component_version_from_git(component, example, branch=None):
    try:
        yml_file_path = get_path_of_yaml(example)
        if not yml_file_path:
            print(f"Failed to get the yaml path of branch {branch}, example {example}, component {component}")
            return None
        version = None
        if branch is None:
            with open(yml_file_path, 'r') as f:
                yml_content = yaml.safe_load(f)
            for dep, dep_value in yml_content.get('dependencies', {}).items():
                if dep.endswith(component):
                    if isinstance(dep_value, dict) and 'version' in dep_value:
                        version = dep_value['version']
                    elif isinstance(dep_value, str):
                        version = dep_value.split(':')[-1].strip()
                    if version:
                        return version
            print(f"Failed to get the yaml path of current branch, example {example}, component {component}")
            return None
        else:
            repo_root = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip()
            relative_path = os.path.relpath(yml_file_path, repo_root)
            result = subprocess.run(['git', 'show', f'{branch}:{relative_path}'], capture_output=True, text=True)
            yml_content = yaml.safe_load(result.stdout)
            for dep, dep_value in yml_content.get('dependencies', {}).items():
                if dep.endswith(component):
                    if isinstance(dep_value, dict) and 'version' in dep_value:
                        version = dep_value['version']
                    elif isinstance(dep_value, str):
                        version = dep_value.split(':')[-1].strip()
                    if version:
                        return version
            print(f"Failed to get the yaml path of branch {branch}, example {example}, component {component}")
            return None
    except Exception as e:
        return None

# Ensure that the old version is less than update_version
def check_version_increment(old_version, update_version, log_print):

    if not all(part.isdigit() for part in old_version.split('.')) or len(old_version.split('.')) != 3:
        print(f"Error: Invalid format for old_version '{old_version}'. It should be in a.b.c format with numbers.")
        return False
    if not all(part.isdigit() for part in update_version.split('.')) or len(update_version.split('.')) != 3:
        if log_print:
            print(f"Error: Invalid format for update_version '{update_version}'. It should be in a.b.c format with numbers.")
        return False
    old_parts = list(map(int, old_version.split('.')))
    update_parts = list(map(int, update_version.split('.')))
    if update_parts[0] < old_parts[0]:
        if log_print:
            print(f"Error: Major version downgrade detected. Update version {update_version} is less than old version {old_version}.")
        return False
    if update_parts[0] == old_parts[0] and update_parts[1] < old_parts[1]:
        if log_print:
            print(f"Error: Minor version downgrade detected. Update version {update_version} is less than old version {old_version}.")
        return False
    if update_parts[0] == old_parts[0] and update_parts[1] == old_parts[1] and update_parts[2] < old_parts[2]:
        if log_print:
            print(f"Error: Patch version downgrade detected. Update version {update_version} is less than old version {old_version}.")
        return False
    return True

# Check if a component has been released based on its version
# 0 means not released, 1 means released, 2 means error occurred
def check_component_release(component_name):
    current_component_version = get_component_version_from_git(component_name)
    main_component_version = get_component_version_from_git(component_name, "origin/main")
    if not current_component_version or not main_component_version:
        print(f"Failed to check release of {component_name}")
        return 2
    print(f"Check release of {component_name}: {main_component_version} --> {current_component_version}")
    if current_component_version != main_component_version:
        return 1
    else:
        return 0

# Check if the example version is influenced by the component version
# 0 means not influenced, 1 means influenced, 2 means error occurred
def check_if_component_influence_example(example_version, component_version):
    tmp_version = process_version(example_version)
    if not tmp_version:
        print(f"Failed to process {example_version}")
        return 2
    if not check_version_increment(tmp_version, component_version, False):
        print(f"component version {component_version} is less than example version {example_version}")
        return 0
    pattern = r'^(~|\^)?\d+\.\d+\.\d+$'
    if not re.match(pattern, example_version):
        print(f"{example_version} is invalid")
        return 2
    prefix = example_version[0] if example_version[0] in ['~', '^'] else ''
    example_parts = list(map(int, example_version.lstrip('~^').split('.')))
    component_parts = list(map(int, component_version.split('.')))
    if not prefix:
        if example_parts[:3] == component_parts[:3]:
            return 1
        else:
            return 0
    if prefix == '~':
        if example_parts[:2] == component_parts[:2]:
            return 1
        else:
            return 0
    if prefix == '^':
        if example_parts[0] == component_parts[0]:
            return 1
        else:
            return 0
    return 2

# Use the local component path to replace the component version in the example yml
def use_local_component_in_yml(component_name, yml_file_path, local_component_path):
    try:
        with open(yml_file_path, 'r') as file:
            lines = file.readlines()
        component_found = False
        indent = ""
        for i, line in enumerate(lines):
            if f"{component_name}:" in line:
                indent = ' ' * (len(line) - len(line.lstrip()))
                if '"' in line and ':' in line:
                    lines[i] = f'{indent}{component_name}:\n{indent}  path: {local_component_path}\n'
                    component_found = True
                    break
                if i + 1 < len(lines) and 'version:' in lines[i+1]:
                    lines[i+1] = f'{indent}  path: {local_component_path}\n'
                    component_found = True
                    break
                print(f"Failed to add local path for {component_name}")
                return False
        if not component_found:
            print(f"Failed to find {component_name} in {yml_file_path} for replacement")
            return False
        with open(yml_file_path, 'w') as file:
            file.writelines(lines)
        return True
    except Exception as e:
        print(f"Failed to use local path '{local_component_path}' for {component_name} of {yml_file_path}")
        return False

def process_version(version):
    pattern = r"^(?:[~^]?)\d+\.\d+\.\d+$"
    if re.match(pattern, version):
        modified_version = version.lstrip('~^')
        return modified_version
    else:
        print(f"Faile to handle version {version}")
        return None

# Component check and attempt to replace. If the component is affected, replace it;
# if the replacement fails, return False.
# Input parameters are the example name and component name
def component_check_and_try_replace(example_name, component_name):
    # 1. When calling the function, ensure that the example is using the component.
    # Otherwise, return False directly. This distinction helps to determine
    # whether the component was not found in the YAML or if parsing failed.

    component_version_used_by_example_current = get_example_component_version_from_git(component_name, example_name)
    if not component_version_used_by_example_current:
        print(f"Cannot find version of {component_name} used by {example_name}")
        return False

    # Now, it's confirmed that the example is using the component
    # 2. Check if the component's version has changed. If not, the component
    # merged after the MR will not be pulled, so we can return immediately
    # without checking if the local component needs to be replaced.
    component_release_flag = check_component_release(component_name)
    if component_release_flag == 0:
        print(f"{component_name} has not been released, skipping check")
        return True
    elif component_release_flag == 2:
        print(f"Failed to check the version change of {component_name}")
        return False

    # Now, it's confirmed that the component's version has changed and
    # the example is using this component
    # 3. We need to determine whether the changed version will affect the current example
    component_version_current = get_component_version_from_git(component_name)
    if not component_version_current:
        print(f"Cannot find version of {component_name}")
        return False
    check_flag = check_if_component_influence_example(component_version_used_by_example_current, component_version_current)
    if check_flag == 2:
        print(f"Failed to check if {component_name} influences {example_name}")
        return False
    elif check_flag == 0:
        print(f"Release of {component_name} will not affect {example_name}, skipping replacement")
        return True

    # Now, it's confirmed that the component has undergone a version change,
    # the example is using the component, and the changed version will impact the example.
    # 4. We need to replace the version used by the example with the local component path.
    component_path = get_path_of_component(component_name)
    if not component_path:
        print(f"Failed to find the path of {component_name}")
        return False
    example_yml_path = get_path_of_yaml(example_name)
    if not example_yml_path:
        print(f"Failed to find the yml of {example_name}")
        return False
    component_relative_path = get_relative_path(example_yml_path, component_path)
    if not component_relative_path:
        print(f"Failed to convert path of {component_name} for {example_name}")
        return False
    replacement_result = use_local_component_in_yml(component_name, example_yml_path, component_relative_path)
    if not replacement_result:
        print(f"Failed to replace {component_name} in {example_name}")
        return False
    else:
        print(f"Replaced {component_name} in {example_name}")
        return True
