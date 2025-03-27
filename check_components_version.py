import sys
import subprocess

# Get the path parameters passed to the script
if len(sys.argv) < 2:
    print("Error: No idf_component.yml path provided")
    sys.exit(1)

file_paths = sys.argv[1:]

# Print the received file paths for debugging
print(f"Processing the following files: {file_paths}")

# Get the version number from the file
def get_version_from_file(file_path):
    try:
        with open(file_path, 'r') as f:
            first_line = f.readline().strip()
            if first_line.startswith('version:'):
                return first_line.split(":")[1].strip().strip('"')
            else:
                print(f"Error: No version found in {file_path}")
                return None
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        return None

# Get the version number from the main branch
def get_version_from_main_branch(file_path):
    try:
        # Get the version number from the main branch of the git repository
        result = subprocess.run(['git', 'show', 'main:' + file_path], capture_output=True, text=True)
        first_line = result.stdout.strip().split('\n')[0]
        if first_line.startswith('version:'):
            return first_line.split(":")[1].strip().strip('"')
        else:
            print(f"Error: No version found in {file_path} from main branch")
            return None
    except Exception as e:
        print(f"Error reading from main branch for {file_path}: {e}")
        return None

# Check the version to ensure the current version is not lower than the main branch version
def check_version_increment(main_version, current_version):
    main_parts = list(map(int, main_version.split('.')))
    current_parts = list(map(int, current_version.split('.')))

    if current_parts[0] < main_parts[0]:
        print(f"Error: Major version downgrade detected. Current version {current_version} is less than main version {main_version}.")
        return False
    if current_parts[0] == main_parts[0] and current_parts[1] < main_parts[1]:
        print(f"Error: Minor version downgrade detected. Current version {current_version} is less than main version {main_version}.")
        return False
    if current_parts[0] == main_parts[0] and current_parts[1] == main_parts[1] and current_parts[2] < main_parts[2]:
        print(f"Error: Patch version downgrade detected. Current version {current_version} is less than main version {main_version}.")
        return False

    return True

# Perform checks for each of the provided file paths
for file_path in file_paths:
    print(f"Processing file: {file_path}")

    # Get the current branch version number
    current_version = get_version_from_file(file_path)
    if current_version is None:
        sys.exit(1)

    # Get the main branch version number
    main_version = get_version_from_main_branch(file_path)
    if main_version is None:
        sys.exit(1)

    # Compare the version numbers
    if not check_version_increment(main_version, current_version):
        sys.exit(1)

print("Version check passed successfully.")
