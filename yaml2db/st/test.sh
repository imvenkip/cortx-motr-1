#!/bin/bash

set -x

# Executable name
EXEC="./yaml2db"

# Input configuration file name
INPUT_FILE="conf.yaml"

# Database path
DB_PATH="./__test_db"

# Parse dump file
PARSE_FILE="parse.txt"

#Emit dump file
EMIT_FILE="emit.txt"

# Number of records to be generated in the configuration file
REC_NR=1000

# Display options of the executable
$EXEC -h

# Generate the configuration file with given records
$EXEC -c $INPUT_FILE -g -n $REC_NR

# Parse the configuration file, store its contents in database
# and dump the key-value pairs in a parse file
$EXEC -c $INPUT_FILE -b $DB_PATH -d -f $PARSE_FILE

# Scan the database and emit the key-value pairs in an emit file
$EXEC -e -b $DB_PATH -d -f $EMIT_FILE

# take diff of the dump files for parsing and emitting
diff $PARSE_FILE $EMIT_FILE

if [ $? == 0 ]
then
	echo "test passed."
else
	echo "test failed."
fi

#rm -rf $PARSE_FILE $EMIT_FILE $INPUT_FILE

sleep 30

rm -rf $DB_PATH*
