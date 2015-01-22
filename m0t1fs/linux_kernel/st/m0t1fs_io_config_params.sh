# Debug level
# - DEBUG_LEVEL_OFF
#   - equivalent to how the ST had been functioning in the past
#   - no debug data would be printed
# - DEBUG_LEVEL_1
#   - linux stob is used instead of ad stob
# - DEBUG_LEVEL_2
#   - including what is covered by DEBUG_LEVEL_1
#   - whole stob contents are printed for the latest file written, after any
#     data discrepancy issue is encountered during the test
#   - this debug level is ideal for testing using automated scripts but when
#     the code is still in development
#   - this way little data is available to refer to from the ST output, in case
#     any issue is encountered
# - DEBUG_LEVEL_3
#   - including what is covered by DEBUG_LEVEL_1
#   - whole stob contents are printed for the latest file written, after each
#     dd execution
#   - whole stob contents are printed for the latest file written, after any
#     data discrepancy issue is encountered during the test (similar to
#     DEBUG_LEVEL_2)
#   - this debug level is ideal to be used with the pattern "ABCD"
#   - this debug level is ideal for testing using automated scripts and when
#     some data is required to debug some issue that is already known to exist
#   - this way, all the stob contents are available in the ST output, from the
#     regular intervals
# - DEBUG_LEVEL_USER_INPUT
#   - including what is covered by DEBUG_LEVEL_1
#   - this mode gives an opportunity to the user to perform some manual
#     debugging at certain intervals e.g. to read some specific part of the
#     stob contents
#   - after each dd execution followed by file compare, it asks the user
#     whether to continue with the script execution and keeps waiting until
#     reply is received
#   - if user enters 'yes' then the script continues
#   - if user enters 'no', then the script unmounts m0t1fs, stops the mero
#     service and then exits
#   - this debug level is ideal for manually debugging some issue that is
#     known to exist
# - DEBUG_LEVEL_TEST
#   - this mode does not at all help mero testing but helps to quickly verify
#     other modifications to the ST script those may be ongoing
#   - mero service is not started
#   - m0t1fs is not created/mounted and instead, data is created in 'tmp'
#     directory inside $dgmode_sandbox directory.
#   - this debug level is ideal to be used while making some modifications to
#     the ST which are to be exercised quickly by avoiding time taken to start
#     services and create and mount m0t1fs

DEBUG_LEVEL_OFF=0   # default
DEBUG_LEVEL_1=1
DEBUG_LEVEL_2=2
DEBUG_LEVEL_3=3
DEBUG_LEVEL_USER_INPUT=4
DEBUG_LEVEL_TEST=5

export debug_level=$DEBUG_LEVEL_OFF

# File pattern to be used while creating data for testing
# - ABCD
#   - Each alphabet from 'a' to 'z' is written 4096 times each serailly
#   - Above pattern unit is repeated until EOF
# - RANDOM1
#   - (RANDOM is a special variable. Hence, not used.)
#   - /dev/urandom is used to have created a random pattern
# - MIXED
#   - io_count is the count of 'the number of times dd has been used
#     in the ST to write to any file'
#   - If $io_count is odd, then RANDOM pattern is used
#   - If $io_count is even, then ABCD pattern is used
#   - This is the default pattern to be used

ABCD="ABCD"
RANDOM1="RANDOM1"
MIXED="MIXED"      # default

export pattern=$MIXED

# ABCD source file sizes (as set in m0t1fs_io_file_pattern.c)
NUM_ALPHABETS=26
NUM_ALPHABET_REPEATITIONS=4096
NUM_ITERATIONS=19
ABCD_SOURCE_SIZE=$(($NUM_ALPHABETS * $NUM_ALPHABET_REPEATITIONS *
		    $NUM_ITERATIONS))
