#!/bin/bash

# Display the usage instructions
display_usage() {
	printf "Usage: $0 <control|process>\n    control: Presents the missing libraries and offers potential replacements.\n    process: Perform the automatic fixing of missing dependencies.\n"
	exit 1
}

# Check the number of arguments
if [ "$#" -ne 1 ]; then
	display_usage
fi

# Check if the argument action matches the two accepted
case $1 in
	"control")
		phase=0
		;;
	"process")
		phase=1
		;;
	*)
		display_usage
		;;
esac

control() {
	# Hash table to store replacements of libraries
	declare -A libs
	local f
	local l

	list() {
		local l
		while read l; do
			# Add the file name to a list to keep track on what program has missing deps
			echo "$1" >> /tmp/broken-deps

			# Check if the library has already been processed
			if [[ ! " ${!libs[@]} " =~ " $l " ]]; then
				libs["$l"]=$(dyngler --query-replace "$l" || echo "NOT FOUND! ($1)")
			fi
		done < <(dyngler --query-missing "$1")
	}

	# Clear the temporary program list
	> /tmp/broken-deps

	# Start by listing all the missing libraries
	while read f; do
		list "$f"
	done > /dev/null 2>&1 < <(find /usr/local/bin -executable -type f ! -name '*.la')

	# Print the summary report of replacements
	printf 'REPORT:\n\n'
	for l in "${!libs[@]}"; do
		echo "$l => ${libs[$l]}"
	done
}

process() {
	local f

	# Check if the temporary program list exists
	if [ -f /tmp/broken-deps ]; then
		# Repair each file
		while IFS="" read -r f; do
			dyngler --repair-deps "$f"
		done < /tmp/broken-deps

		# Remove the temporary list
		rm -f /tmp/broken-deps
	else
		# Repair every executable we find
		while read f; do
			dyngler --repair-deps "$f"
		done < <(find /usr/local/bin -executable -type f ! -name '*.la')
	fi
}

# Check if dyngler is present
if ! command -v 'dyngler' &> /dev/null; then
	printf 'This script requires the dynamics-wrangler program!\nhttps://github.com/RubisetCie/dynamics-wrangler\n' 1>&2
	exit 2
fi

# Branch the actual phase
case $phase in
	0)
		control
		;;
	1)
		process
		;;
esac
