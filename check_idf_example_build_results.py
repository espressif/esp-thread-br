import os
import re
import subprocess
import sys

# Check the type of version update
def check_version_update(a2, a3):
    # Split version numbers into three parts for comparison
    a2_parts = list(map(int, a2.split('.')))
    a3_parts = list(map(int, a3.split('.')))

    if a2_parts != a3_parts:
        if a2_parts[0] != a3_parts[0]:
            return "Major update"
        elif a2_parts[1] != a3_parts[1]:
            return "Minor update"
        elif a2_parts[2] != a3_parts[2]:
            return "Patch update"
    return "No update"

# Extract example_version from the file
def get_example_version(file_path, components_name):
    try:
        with open(file_path, 'r') as file:
            content = file.read()
            # If "Success" is found, return 0 directly
            if "Success" in content:
                print("Found 'Success' in the file. Returning 0.")
                return 0
            # If components_name is not found, return 0
            if components_name not in content:
                print(f"'{components_name}' not found in the file. Returning 0.")
                return 0
            # Search for version after components_name
            pattern = re.compile(rf"{components_name}:\s*([~^]?\d+\.\d+\.\d+)")
            match = pattern.search(content)
            if match:
                example_version = match.group(1)
                print(f"Found example_version: {example_version}")
                return example_version
            else:
                print(f"No valid version found after '{components_name}'. Returning 1.")
                return 1
    except Exception as e:
        print(f"Error reading the file: {e}")
        return 1

# Extract version number from the content of the YAML file
def get_version_from_yaml_from_content(content):
    # Get the first line of the file content and extract the version number
    first_line = content.splitlines()[0].strip()
    print(f"First line from YAML content: {first_line}")  # Debug output
    match = re.match(r'version:\s*"(\d+\.\d+\.\d+)"', first_line)
    if match:
        return match.group(1)
    else:
        print("Invalid version format in idf_component.yml content. Returning None.")
        return None

# Get the version number from the current branch (directly read from the file)
def get_current_branch_version(path):
    try:
        print(f"Fetching version from current branch for path: {path}")
        # Read the idf_component.yml file from the current branch
        with open(path, 'r') as file:
            content = file.read()
        # Extract version number from file content
        return get_version_from_yaml_from_content(content)
    except Exception as e:
        print(f"Error reading current branch idf_component.yml: {e}")
        return None

# Get the version number from the main branch
def get_main_branch_version(path):
    try:
        print(f"Fetching version from main branch for path: {path}")
        # Use git show to get the version number from the main branch
        command = f"git show origin/main:{path} --quiet"
        result = subprocess.run(command, shell=True, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Error getting version from main branch: {result.stderr}")
            return None
        # Extract version number from the content fetched by git show
        return get_version_from_yaml_from_content(result.stdout)  # Use the content from git show
    except Exception as e:
        print(f"Error fetching version from main branch: {e}")
        return None

def main(components_path, file_path):
    # Derive component name from the component path
    components_name = os.path.basename(components_path)

    # Read the name.txt file to get the example_version
    example_version = get_example_version(file_path, components_name)
    if example_version == 0:
        print(f"Skip check because the build passed or the example doesn't use '{components_name}'")
        return 0
    if example_version == 1:
        print(f"Check failed because no valid version of '{components_name}' is used by the example")
        return 1

    # Get version numbers from current branch and main branch
    yaml_path = os.path.join(components_path, "idf_component.yml")
    current_version = get_current_branch_version(yaml_path)
    main_version = get_main_branch_version(yaml_path)

    if current_version is None or main_version is None:
        return 1

    print(f"Current branch version: {current_version}")
    print(f"Main branch version: {main_version}")

    # Check the type of version update
    update_type = check_version_update(main_version, current_version)
    print(f"Version update type: {update_type}")
    if (update_type == "No update"):
        print(f"Skip check due to no updates or version release")
        return 0

    # Based on example_version, perform the corresponding checks
    if example_version.startswith('~'):
        base_version = '.'.join(example_version[1:].split('.')[0:2])  # Return a.b
        if current_version.startswith(base_version):
            if update_type in ["Major update", "Minor update"]:
                print("Major or minor update, failure allowed")
                return 0
            else:
                print("Patch update, failure not allowed")
                return 1
        else:
            print("Version change doesn't affect the example, failure allowed")
            return 0
    elif example_version.startswith('^'):
        base_version = example_version[1:].split('.')[0]
        if current_version.split('.')[0] == base_version.split('.')[0]:
            if update_type == "Major update":
                print("Major update, failure allowed")
                return 0
            else:
                print("Minor or patch update, failure not allowed")
                return 1
        else:
            print("Version change doesn't affect the example, failure allowed")
            return 0
    else:
        print("Example uses a fixed version, failure allowed")
        return 0

if __name__ == "__main__":
    # Read input parameters
    components_path = sys.argv[1]
    file_path = sys.argv[2]

    # Execute the main logic
    result = main(components_path, file_path)
    sys.exit(result)
