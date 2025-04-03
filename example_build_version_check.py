import sys
import tools.ci.version_check_tools as vct

def main():
    if len(sys.argv) != 3:
        print("Usage: python script.py <example_name> <component_name>")
        sys.exit(1)

    example_name = sys.argv[1]
    component_name = sys.argv[2]

    result = vct.component_check_and_try_replace(example_name, component_name)

    sys.exit(0 if result else 1)

if __name__ == "__main__":
    main()
