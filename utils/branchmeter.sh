#!/bin/bash
#
# Print information about the branch
#
print_branch_skew()
{
	#
	# Get the last component in the path
	#
	name=`basename $1`

	#
	# Determine how ahead is master
	#
	commit_lag=`git log $(git merge-base ${1} origin/master)..${1} | wc -l`

	#
	# Check last merge date to/from master
	#
	last_merge=`git log --merges --pretty=short --format="%H %ai" $(git merge-base ${1} origin/master)..${1}| head -1 |  awk '{print $2}'`

	if [ "$last_merge" == "" ]; then
		#
		# If there are no merges, check the branch creation date
		#
		last_merge=`git log --pretty=short --format="%H %ai" $(git merge-base ${1} origin/master)..${1} | tail -1 | awk '{ print $2}'`
	fi

	if [ "$last_merge" == "" ]; then
		#
		# If no date info is available, this branch is not off of master
		#
		last_merge="Not-a-branch-of-master?"
	fi

	#
	# Print branch information.
	#
	printf "%-30s\t%6d\t\t%s\n" $name $commit_lag $last_merge
}


###########
# Main
###########

#
# Check if the CWD is a work dir for a git repository.
#
if [ ! -d .git ]; then
	echo "This is not a working dir for git repository"
	exit 1
fi

#
# Get list of all branches
#
branch_list=`git branch -a | grep remotes | grep -v master`
printf "Branch Name \t Commits behind master \t    Last merge (or branch) date\n"

#
# Print required information for each branch
#
for branch in $branch_list; do
	print_branch_skew $branch
done
