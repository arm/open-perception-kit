#!/bin/bash

build_docker() {
	FILE="./.devcontainer/Dockerfile.template"
	FINAL=$( [ -f "$FILE" ] && cat "$FILE" || echo "" )

	OUT="./.devcontainer/Dockerfile"
	echo "$FINAL" > $OUT
	
	echo "Saved $OUT"
}

build_devcontainer() {
	FILE="./.devcontainer/devcontainer.template.json"
	FINAL=$( [ -f "$FILE" ] && cat "$FILE" || echo "" )
	
	FILE="./.devcontainer/runargs.$1"
	RUNARGS=$( [ -f "$FILE" ] && cat "$FILE" || echo "" )
	FILE="./.devcontainer/env.$1"
	ENV=$( [ -f "$FILE" ] && cat "$FILE" || echo "" )
	
	FINAL=${FINAL//%RUNARGS%/$RUNARGS}
	FINAL=${FINAL//%ENV%/$ENV}

	OUT="./.devcontainer/devcontainer.json"
	echo "$FINAL" > $OUT
	
	echo "Saved $OUT"
}

build() {
    build_docker $1
    build_devcontainer $1
}

while true; do
    echo "Please select an option:"
    echo "1) Default container"
    echo "2) PC hardware accelerated x64 Linux"
    echo "q) Quit"
    read -rp "Enter choice: " choice

    case $choice in
        1)
        	build "default"
        	break
        	;;
        2)
        	build "hw_acc_x64_pc"
        	break
            ;;
        q)
        	echo "Exiting.."
            break
            ;;
        *)
            echo "Invalid option. Try again."
            ;;
    esac

    echo "Press Enter to continue..."
    read
done