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
#       * Clone colibri source
#       * build source
#       * Run UT's and UB's
#       * Generate coverage data

# NOTES:
#       Currently kernel UT Part is not implemented
#       The Script must be run as root

usage()
{
    echo "Test automation script for colibri"
    echo "run_tests.sh -r <path> -b [all | i=a,b,... | e=a,b,... | current]"
    echo "            [-d <path>] [-h] [-m <e-mail-id>] [-s suffix]"

    echo ""
    echo "Where:"
    echo "-b <branches>  Branches to operate on"
    echo "                      all      - All branches"
    echo "                      i=<list> - Include branches from <list>"
    echo "                      e=<list> - Exclude branches from <list>"
    echo "                      current  - Current branch"
    echo "-d <path>      Directory from where Colibri source is to be used"
    echo "                 (If this is not given UB will not be run)"
    echo "-h             Print this help"
    echo "-m <e-mail-id> Send mail to this email address in case of error or success."
    echo "-r <path>      Test root directory (Mandatory)."
    echo "-s <suffix>    Suffix to be appended to top level directory name in "
    echo "                 case user wants to differentiate between test directories."
}

OPTIONS_STRING="b:d:hm:r:s:"

# Directory structure:
# The utility will create the following directory structure on invocation,

# $TESTROOT/
# └── $DIRTIME/
#     ├── run_tests.log
#     ├── branches.txt
#     ├── <Branch Name>/ [ For each branch ]
#     │   ├── core-dumps/
#     │   ├── coverage-data/
#     │   │   ├── html/
#     │   │   └── coverage-data.txt
#     │   └── logs/
#     │       ├── autogen.sh.log
#     │       ├── configure.log
#     │       ├── cov.log
#     │       ├── git.log
#     │       ├── make.log
#     │       ├── make-clean.log
#     │       ├── ut.log
#     │       └── ub.log
#     └── src-YYYY-MM-DD_HH-MM-SS/ [ If colibri repository is given on command
#                                    line then src is a link to the
#                                    colibri repository otherwise it is a
#                                    cloned colibri repository ]

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


# NOTE(S):
# For sending mail it is assumed that mail command is configured

#-------------------------------------------------------------------------------

# Directories to be created if not present
DIRS="logs core-dumps coverage-data coverage-data/html"

# GIT Variables
# REMOTE_GIT_CMD="git clone ssh://gitosis@git.clusterstor.com/colibri.git"
GIT_USER=${GIT_USER:-gitosis}
GIT_WEB_ADDRESS=${GIT_WEB_ADDRESS:-git.clusterstor.com}
GIT_PROTOCOL=${GIT_PROTOCOL:-ssh}
GIT_REPOSITORY=${GIT_REPOSITORY:-colibri.git}

UB_ROUNDS=${UB_ROUNDS:-0}

# For setting any of the above variables do,
# UB_ROUNDS=NUM ./run_tests.sh [OPTIONS ...]

MAIL_TEXT_FILE=$(mktemp /tmp/build-auto-mail-log.XXXXXX) > /dev/null

COLIBRI_CORE_PATH=""

#-------------------------------------------------------------------------------

# print_msg arg1 arg2
# arg1 - Echo the message to console
# arg2 - If arg2 == 1 then put arg1 in e-mail also

print_msg()
{
    mail_text=$(echo $(date +"%Y-%m-%d:%H-%M-%S")":$1")
    echo $mail_text >> $TESTROOT/$DIRTIME/run_tests.log

    if [ "$2" = "1" ]; then
        echo $mail_text >> $MAIL_TEXT_FILE
    fi
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
    if [ "$CMD_LOG" = "" ]; then
        CMD_LOG="$CMD"
    fi

    cur_branch_dir="$TESTROOT/$DIRTIME/$current_branch"
    pushd $DIR > /dev/null

    print_msg "INFO:$current_branch:$CMD_LOG:Start"

    $CMD $ARG >> "$cur_branch_dir/logs/$CMD_LOG.log" 2>&1
    rc=$?
    if [ "$rc" != "0" ]; then
        print_msg "ERR:$current_branch:$rc:$CMD_LOG failed" 1
    fi
    if [ "$CORE_DUMP_FLAG" = "1" ]; then
        copy_core_dump
    fi

    print_msg "INFO:$current_branch:$CMD_LOG:End"

    popd > /dev/null
    return $rc
}

copy_core_dump()
{
    cur_branch_dir="$TESTROOT/$DIRTIME/$current_branch"

    if [ -f $COLIBRI_CORE_PATH/utils/ut-sandbox/core.* ]; then
        cp $COLIBRI_CORE_PATH/utils/ut-sandbox/core.* $cur_branch_dir/core-dumps/
        cp $COLIBRI_CORE_PATH/utils/.libs/lt-ut $cur_branch_dir/core-dumps/
        print_msg "INFO:::A core is copied at $cur_branch_dir/core-dumps/" 1
    fi
}

gather_coverage()
{
    cur_branch_dir="$TESTROOT/$DIRTIME/$current_branch"
    pushd  $COLIBRI_CORE_PATH/utils > /dev/null

    if [ -f gcov_stats_genhtml.sh ]; then
        print_msg "INFO:$current_branch:Coverage:Start"
        ./gcov_stats_genhtml.sh user $COLIBRI_CORE_PATH \
            $cur_branch_dir/coverage-data/html >>       \
            $cur_branch_dir/logs/cov.log 2>&1
        rc=$?
        if [ $rc -ne 0 ]; then
            print_msg "ERR:$current_branch:Coverage:$rc:Gathering coverage \
                data failed" 1
        else
            if [ -f process_lcov.sh ]; then
                ./process_lcov.sh \
		    -i $cur_branch_dir/coverage-data/html/index.html -b -l -f \
                    > $cur_branch_dir/coverage-data/coverage-data.txt 2>&1
                rc=$?
                if [ $rc -ne 0 ]; then
                    print_msg "ERR:$current_branch:Coverage:$rc:Gathering \
                        coverage data failed" 1
                fi
            fi
        fi
    else
        print_msg "ERR:$current_branch:Coverage:$rc:gcov_stats_genhtml.sh \
                not found" 1
        rc=2
    fi
    print_msg "INFO:$current_branch:Coverage:End"
    popd > /dev/null
    return $rc
}

run_test_automate()
{
    for line in $(cat $TESTROOT/$DIRTIME/branches.txt); do
        current_branch=$(basename $line)
        BRANCH_NAMES="$current_branch"", $BRANCH_NAMES"
        create_dir $TESTROOT/$DIRTIME/$current_branch
        rc=$?
        if [ $rc -ne 0 ]; then
            return $rc
        fi

        for directory in $DIRS; do
            create_dir $TESTROOT/$DIRTIME/$current_branch/$directory
            rc=$?
            if [ $rc -ne 0 ]; then
                return $rc
            fi
        done

        git checkout $current_branch >> \
                $TESTROOT/$DIRTIME/$current_branch/logs/git.log 2>&1

        if run_command "$COLIBRI_SOURCE/core" 'sh autogen.sh'                    && \
           run_command "$COLIBRI_SOURCE/core" './configure' '--enable-coverage'  && \
           run_command "$COLIBRI_SOURCE/core" 'make'; then

            run_command "$COLIBRI_SOURCE/core/utils" './ut' '' 1

            if [ $UB_ROUNDS -ne 0 ]; then
                run_command "$COLIBRI_SOURCE/core/utils" './ub' $UB_ROUNDS
            fi

            gather_coverage
        fi

        print_msg "INFO:$current_branch:Cleanup:Start"
        cd $COLIBRI_CORE_PATH
        make distclean >> \
                $TESTROOT/$DIRTIME/$current_branch/logs/make-clean.log 2>&1
        print_msg "INFO:$current_branch:Cleanup:End"
    done

    return 0
}

parse_branches()
{
    tempfile=$(mktemp)

    case $BRANCHES in
        all)
            return 0
            ;;

        current)
            pushd $COLIBRI_SOURCE/core > /dev/null
            git branch | grep \* | awk '{print $2}' > \
                $TESTROOT/$DIRTIME/branches.txt
            popd > /dev/null
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
    if [ "$suffix" = "" ]; then
        DIRTIME=$(date +"%Y-%b-%d_%H-%M-%S")
    else
        DIRTIME=$(date +"%Y-%b-%d_%H-%M-%S")"-$suffix"
    fi

    COLIBRI_SOURCE=$TESTROOT/$DIRTIME/src"-$DIRTIME"

    create_dir $TESTROOT/$DIRTIME
    rc=$?
    if [ $rc -ne 0 ]; then
        return $rc
    fi

    if [ "$src_dir" = "" ]; then
        create_dir $COLIBRI_SOURCE
        rc=$?
        if [ $rc -ne 0 ]; then
            return $rc
        fi

        print_msg "INFO:Cloning Colibri source please wait..."

        # Clone the Colibri source
        git clone $GIT_PROTOCOL://$GIT_USER@$GIT_WEB_ADDRESS/$GIT_REPOSITORY \
                $COLIBRI_SOURCE >> $TESTROOT/$DIRTIME/run_tests.log 2>&1
        rc=$?
        if [ $rc -ne 0 ]; then
            print_msg "ERR:::$rc:Cloning the Colibri source failed" 1
            return $rc
        fi

        if [ -f $COLIBRI_SOURCE/checkout ]; then
            cd $COLIBRI_SOURCE
            ./checkout >> $TESTROOT/$DIRTIME/run_tests.log 2>&1
            rc=$?
            if [ $? -ne 0 ]; then
                print_msg "ERR:::$rc:Cloning the Colibri source failed" 1
                return $rc
            fi
        else
            print_msg "ERR:::$rc:No checkout script found in source" 1
            return 1
        fi
    else # [ "$src_dir" != "" ]
        ln -s $src_dir $COLIBRI_SOURCE
    fi

    pushd $COLIBRI_SOURCE/core > /dev/null

    git branch -a | grep origin/ | grep -v HEAD > \
        $TESTROOT/$DIRTIME/branches.txt

    parse_branches
    rc=$?
    if [ $? -ne 0 ]; then
        print_msg "ERR:::$rc:Branch parsing failed" 1
        return $rc
    fi

    COLIBRI_CORE_PATH=$COLIBRI_SOURCE/core

    popd > /dev/null
}

check_and_setup_environ()
{
    which lcov > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        print_msg "ERR:::$rc:lcov not present" 1
        return 2
    fi

    ulimit -c unlimited > /dev/null 2>&1

    echo "" > $MAIL_TEXT_FILE

    return 0
}

send_email()
{
    if [ "$MAIL_ADDRESS" != "" ]; then
        if [ $1 -eq 0 ]; then
            curr_hostname=$(hostname)
            SUCCESS_MSG="Following branches were successfully tested - \
                $BRANCH_NAMES. The results can be found at $TESTROOT/$DIRTIME/"

            echo $SUCCESS_MSG | mail -s "Colibri build automation script ran \
                successfully on $curr_hostname" $MAIL_ADDRESS
        else
            echo $MAIL_TEXT_FILE | mail -s "Error in Colibri build automation \
                script on $curr_hostname" $MAIL_ADDRESS
        fi
    fi
    mv $MAIL_TEXT_FILE $TESTROOT/$DIRTIME/mail.log
}

main()
{
    check_and_setup_environ
    rc=$?
    if [ $rc -ne 0 ]; then
        exit $rc
    fi

    init_dirs
    rc=$?
    if [ $rc -ne 0 ]; then
        exit $rc
    fi

    run_test_automate

    send_email $?

    return 0
}

if [ "$(id -u)" != "0" ]; then
   echo "This script must be run as root" 1>&2
   exit 1
fi

if [ $# -lt 2 ]; then
    usage
    exit 1;
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

        m)
            MAIL_ADDRESS="$OPTARG"
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
        print_msg "ERR:::2:Directory not found" 1
        exit 2
    fi
fi

if [ "$BRANCHES" = "" ]; then
    print_msg "BRANCHES not specified"
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
