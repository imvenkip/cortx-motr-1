#!/bin/bash
#
# COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
#
# THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
# HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
# LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
# THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
# BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
# USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
# EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
#
# YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
# THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
# http://www.xyratex.com/contact
#
# Original author: Manish Honap <manish_honap@xyratex.com>
# Original creation date: 02/03/2012
#

# Script for
#       * Clone mero source
#       * build source
#       * Run UT's and UB's
#       * Generate coverage data

# NOTES:
#       Currently kernel UT Part is not implemented
#       The Script must be run as root

usage()
{
    echo "Test automation script for mero"
    echo "mero_build_test.sh -r <path> -b [all | i=a,b,... | e=a,b,... | current]"
    echo "                     [-d <path>] [-h] [-s suffix]"

    echo ""
    echo "Where:"
    echo "-b <branches>  Branches to operate on"
    echo "                      all      - All branches"
    echo "                      i=<list> - Include branches from <list>"
    echo "                      e=<list> - Exclude branches from <list>"
    echo "                      current  - Current branch"
    echo "-d <path>      Directory from where Mero source is to be used"
    echo "-h             Print this help"
    echo "-r <path>      Test root directory (Mandatory)."
    echo "-s <suffix>    Suffix to be appended to top level directory name in "
    echo "               case user wants to differentiate between test directories."
}

OPTIONS_STRING="b:d:hr:s:"

# Directory structure:
# The utility will create the following directory structure on invocation,

# $TESTROOT/
# |-- $DIRTIME/
#     |-- mero_build_test.log
#     |-- branches.txt
#     |-- <Branch Name>/ [ For each branch ]
#     |   |-- core-dumps/
#     │   |-- coverage-data/
#     │   │   |-- html/
#     │   │   |-- coverage-data.txt
#     │   |-- logs/
#     │       |-- autogen.sh.log
#     │       |-- configure.log
#     │       |-- cov.log
#     │       |-- git.log
#     │       |-- make.log
#     │       |-- make-clean.log
#     │       |-- ut.log
#     │       |-- ub.log
#     |-- src[-YYYY-MM-DD_HH-MM-SS/] [ If mero repository is given on command
#     │                                 line then src is a link to the
#     │                                 mero repository otherwise it is a
#     │                                 cloned mero repository ]
#     |-- sandbox/                   [ All tests will be run from this directory ]

# The messages will be printed in following format;
# DATE:TIME:CODE:BRANCH:PHASE:$rc:MESSAGE
# Where ( * represents Optional ):
#       DATE    - date in $yyyy-$mm-$dd format
#       TIME    - time in $hh-$mm-ss format
#       CODE    - can be any of the [ INFO | ERR ]
#       BRANCH  - current branch (*)
#       PHASE   - can be any of the following,
#                 [ autogen | configure | make | UT | UB | Coverage ] (*)
#       $rc     - Error code (*)
#       MESSAGE - Detailed message
# Messages with code `ERR' contain $rc as error code.


#-------------------------------------------------------------------------------

# Directories to be created if not present
DIRS="logs core-dumps coverage-data coverage-data/html"

# GIT Variables
# REMOTE_GIT_CMD="git clone --recursive ssh://gitosis@git.clusterstor.com/mero.git"

# For setting any of the variables below, do -
# UB_ROUNDS=NUM ./mero_build_test.sh [OPTIONS ...]

GIT_USER=${GIT_USER:-gitosis}
GIT_WEB_ADDRESS=${GIT_WEB_ADDRESS:-git.clusterstor.com}
GIT_PROTOCOL=${GIT_PROTOCOL:-ssh}
GIT_REPOSITORY=${GIT_REPOSITORY:-mero.git}

UB_ROUNDS=${UB_ROUNDS:-0}
DATE_BASED_DIR_NAME=${DATE_BASED_DIR_NAME:-0}

MERO_CORE_PATH=""

#-------------------------------------------------------------------------------

# print_msg arg1
# arg1 - Message to be echoed
print_msg()
{
    msg=$(echo $(date +"%Y-%m-%d:%H-%M-%S")":$1")
    echo $msg >> $TESTROOT/$DIRTIME/mero_build_test.log
}

# create_dir arg1
# arg1 - Directory to be created
create_dir()
{
    rc=0
    if [ ! -d "$1" ]; then
        mkdir "$1"
        rc=$?
        if [ $rc -ne 0 ]; then
            print_msg "ERR:::$rc:Creating $1 failed"
        fi
    fi
    return $rc
}

#run_command DIR CMD ARG CORE_DUMP_FLAG
# Run the command `CMD' in `DIR' with argument `ARG'
# If CORE_DUMP_FLAG=1 then invoke copy_core_dump
run_command()
{
    DIR=$1
    CMD=$2
    ARG=$3
    CORE_DUMP_FLAG=$4

    CMD_LOG=$(echo $CMD| awk '{ print $2 }')
    CMD_LOG=$(basename 2>/dev/null $CMD_LOG)
    if [ "$CMD_LOG" = "" ]; then
        CMD_LOG="$CMD"
    fi

    cur_branch_dir="$TESTROOT/$DIRTIME/$current_branch"
    pushd $DIR > /dev/null

    print_msg "INFO:$current_branch:$CMD_LOG:Start"

    $CMD $ARG >> "$cur_branch_dir/logs/$CMD_LOG.log" 2>&1
    rc=$?
    if [ "$rc" != "0" ]; then
        print_msg "ERR:$current_branch:$rc:$CMD_LOG failed"
        if [ "$CORE_DUMP_FLAG" = "1" ]; then
            copy_core_dump
	fi
    else
	print_msg "INFO:$current_branch:$CMD_LOG:End"
    fi

    popd 2> /dev/null > /dev/null
    return $rc
}

copy_core_dump()
{
    if [ "$(id -u)" -eq 0 ]; then
	cur_branch_dir="$TESTROOT/$DIRTIME/$current_branch"
	cp $TESTROOT/$DIRTIME/sandbox/ut-sandbox/core.* $cur_branch_dir/core-dumps/ &> /dev/null
	rc=$?
	sudo cp $MERO_CORE_PATH/utils/.libs/lt-ut $cur_branch_dir/core-dumps/ &> /dev/null
	if [ $rc -eq 0 ]; then
	    print_msg "INFO:::A core is copied at $cur_branch_dir/core-dumps/"
	fi
    else
	print_msg "INFO:::A core is generated at $TESTROOT/$DIRTIME/sandbox/ut-sandbox/"
    fi
}

gather_coverage()
{
    cur_branch_dir="$TESTROOT/$DIRTIME/$current_branch"
    pushd  $MERO_CORE_PATH/scripts > /dev/null

    if [ -f gcov_stats_genhtml.sh ]; then
        print_msg "INFO:$current_branch:Coverage:Start"
        sudo ./gcov_stats_genhtml.sh user $MERO_CORE_PATH \
            $cur_branch_dir/coverage-data/html >>       \
            $cur_branch_dir/logs/cov.log 2>&1
        rc=$?
        if [ $rc -ne 0 ]; then
            print_msg "ERR:$current_branch:Coverage:$rc:Gathering coverage \
                data failed"
        else
            if [ -f process_lcov.sh ]; then
                sudo ./process_lcov.sh \
		    -i $cur_branch_dir/coverage-data/html/index.html -b -l -f \
                    > $cur_branch_dir/coverage-data/coverage-data.txt 2>&1
                rc=$?
                if [ $rc -ne 0 ]; then
                    print_msg "ERR:$current_branch:Coverage:$rc:Gathering \
                        coverage data failed"
                fi
            fi
        fi
    else
        print_msg "ERR:$current_branch:Coverage:$rc:gcov_stats_genhtml.sh \
                not found"
        rc=2
    fi
    print_msg "INFO:$current_branch:Coverage:End"
    popd 2> /dev/null > /dev/null
    return $rc
}

run_test_automate()
{
    for line in $(cat $TESTROOT/$DIRTIME/branches.txt); do
        current_branch=$(basename $line)
        BRANCH_NAMES="$current_branch"", $BRANCH_NAMES"
        create_dir $TESTROOT/$DIRTIME/$current_branch || return $?

        for directory in $DIRS; do
            create_dir $TESTROOT/$DIRTIME/$current_branch/$directory || return $?
        done

        git checkout $current_branch >> \
                $TESTROOT/$DIRTIME/$current_branch/logs/git.log 2>&1

        if run_command "$MERO_SOURCE" 'sh autogen.sh'                    && \
           run_command "$MERO_SOURCE" './configure' '--enable-coverage'  && \
           run_command "$MERO_SOURCE" 'make'; then

	    pushd 2>/dev/null $TESTROOT/$DIRTIME/sandbox > /dev/null
            run_command '' "sudo $MERO_SOURCE/utils/ut.sh" '' 1
	    rc=$?
	    popd &>/dev/null

            if [ $UB_ROUNDS -ne 0 ] && [ $rc -eq 0 ]; then
                run_command "$MERO_SOURCE/utils" 'sudo ./ub' $UB_ROUNDS
		rc=$?
            fi

	    if [ $rc -eq 0 ]; then
		gather_coverage
		rc=$?
	    fi
	else
	    rc=$?
        fi

        print_msg "INFO:$current_branch:Cleanup:Start"
        cd $MERO_CORE_PATH > /dev/null
        make distclean >> \
                $TESTROOT/$DIRTIME/$current_branch/logs/make-clean.log 2>&1
        print_msg "INFO:$current_branch:Cleanup:End"
    done

    return $rc
}

parse_branches()
{
    tempfile=$(mktemp)

    case $BRANCHES in
        all)
            return 0
            ;;

        current)
            pushd $MERO_SOURCE > /dev/null
            git branch | grep \* | awk '{print $2}' > \
                $TESTROOT/$DIRTIME/branches.txt
            popd 2> /dev/null > /dev/null
            return 0
            ;;

        i=*)
            tempbranches=$(mktemp)
            echo $BRANCHES | sed 's/i=//' | awk ' BEGIN { RS="," } {print} ' |\
                 grep -v "^$" > \
                $tempfile
            cp $TESTROOT/$DIRTIME/branches.txt $tempbranches
            cat /dev/null > $TESTROOT/$DIRTIME/branches.txt > /dev/null
            for line in $(cat $tempfile); do
                cat $tempbranches | grep $line > /dev/null
                rc=$?
                if [ $rc -ne 0 ]; then
                    print_msg "ERR:::$rc:Branch $line not found"
                else
                    echo $line >> $TESTROOT/$DIRTIME/branches.txt
                fi
            done
            rm $tempbranches > /dev/null
            ;;

        e=*)
            NEW_BRANCHES=$(echo $BRANCHES | sed 's/i=//' | awk ' BEGIN \
                { RS="," } {ORS=" "} {print}')

            while read line; do
                for branch in $NEW_BRANCHES; do
                    if [ $(basename $line) !=  "$branch" ]; then
                        echo $line >> $tempfile
                    fi
                done
            done < $TESTROOT/$DIRTIME/branches.txt
            mv $tempfile $TESTROOT/$DIRTIME/branches.txt
            ;;

        *)
           return 1;
           ;;

    esac
    return 0;
}

init_dirs()
{
    if [ "$DATE_BASED_DIR_NAME" = "1" ]; then
	if [ "$suffix" = "" ]; then
            DIRTIME=$(date +"%Y-%b-%d_%H-%M-%S")
	else
            DIRTIME=$(date +"%Y-%b-%d_%H-%M-%S")"-$suffix"
	fi
	MERO_SOURCE=$TESTROOT/$DIRTIME/src"-$DIRTIME"
    else
	DIRTIME=""
	MERO_SOURCE=$TESTROOT/$DIRTIME/src
    fi

    create_dir $TESTROOT/$DIRTIME || return $?

    create_dir $TESTROOT/$DIRTIME/sandbox || return $?

    if [ "$src_dir" = "" ]; then
        create_dir $MERO_SOURCE || return $?

        print_msg "INFO:Cloning Mero source please wait..."

        # Clone the Mero source
        git clone --recursive $GIT_PROTOCOL://$GIT_USER@$GIT_WEB_ADDRESS/$GIT_REPOSITORY \
                $MERO_SOURCE >> $TESTROOT/$DIRTIME/mero_build_test.log 2>&1 || return $?
    else # [ "$src_dir" != "" ]
        ln -s $src_dir $MERO_SOURCE
    fi

    pushd $MERO_SOURCE > /dev/null

    git branch -a | grep origin/ | grep -v HEAD > \
        $TESTROOT/$DIRTIME/branches.txt

    parse_branches || return $?

    MERO_CORE_PATH=$MERO_SOURCE

    popd 2> /dev/null > /dev/null
}

check_and_setup_environ()
{
    which lcov > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        print_msg "ERR:::$rc:lcov not present"
        return 2
    fi

    ulimit -c unlimited > /dev/null 2>&1

    return 0
}

main()
{
    cd $TESTROOT/$DIRTIME > /dev/null
    check_and_setup_environ || exit $?

    init_dirs || exit $?

    cd $TESTROOT/$DIRTIME/sandbox > /dev/null
    run_test_automate || exit $?
}

if [ $# -lt 2 ]; then
    usage
    exit 1
fi

while getopts "$OPTIONS_STRING" OPTION; do
    case "$OPTION" in
        b)
            BRANCHES="$OPTARG"
            ;;

        d)
            src_dir="$OPTARG"
            ;;

        h)
            usage
            exit 0
            ;;

        r)
            TESTROOT="$OPTARG"
            ;;

        s)
            suffix="$OPTARG"
            ;;

        *)
            usage
            exit 1
            ;;

    esac
done

if [ "$TESTROOT" = "" ]; then
    print_msg "TESTROOT not specified"
    usage
    exit 1
else
    if ! [ -d $TESTROOT ]; then
        print_msg "ERR:::2:Directory not found"
        exit 2
    fi
fi

if [ "$BRANCHES" = "" ]; then
    print_msg "Branch not specified"
    usage
    exit 1
fi

letter1=$(expr substr $BRANCHES 1 1)
letter2=$(expr substr $BRANCHES 2 1)

if [ "$letter1" = "i" ] || [ "$letter1" = "e" ]; then
    if [ "$letter2" != "=" ]; then
        echo "Syntax error in specifying branches for -b option"
        usage
        exit 1
    fi
else
    if [ "$BRANCHES" != "all" ] && [ "$BRANCHES" != "current" ]; then
        echo "Syntax error in specifying branches for -b option"
        usage
        exit 1
    fi
fi

main
