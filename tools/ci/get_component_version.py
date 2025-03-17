import sys
import yaml

def extract_version(file_a, file_b, component):
    try:
        with open(file_a, 'r', encoding='utf-8') as f:
            data = yaml.safe_load(f)  # Parse YAML
    except Exception as e:
        print(f"Error reading {file_a}: {e}")
        sys.exit(1)

    version = None
    dependencies = data.get('dependencies', {})

    # Check if the component is in the dependencies section
    if component in dependencies:
        value = dependencies[component]
        if isinstance(value, dict) and 'version' in value:
            version = value['version']
        elif isinstance(value, str):  # Direct version number
            version = value

    # If not found, iterate through the dependencies to find the component
    if version is None:
        for key, value in dependencies.items():
            if key.split('/')[-1] == component:
                if isinstance(value, dict) and 'version' in value:
                    version = value['version']
                elif isinstance(value, str):  # Direct version number
                    version = value
                break

    # If the component is not found, exit with an error
    if version is None:
        print(f"Component {component} not found in {file_a}.")
        sys.exit(1)

    # Prepare the result string with component and its version
    result = f"{component}: {version}\n"

    # Try to open the file and append the result
    try:
        with open(file_b, 'a', encoding='utf-8') as f:
            f.write(result)
    except Exception as e:
        print(f"Error writing to {file_b}: {e}")
        sys.exit(1)

    print("Operation completed successfully.")

if __name__ == "__main__":
    # Check for correct number of arguments
    if len(sys.argv) != 4:
        print("Usage: python3 get_components_version.py <source.yml> <result.txt> <component_name>")
        sys.exit(1)

    file_a = sys.argv[1]
    file_b = sys.argv[2]
    component = sys.argv[3]

    extract_version(file_a, file_b, component)
