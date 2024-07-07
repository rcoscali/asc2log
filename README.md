# asc2log

Added some useful options as '-t' for fixing time origin, '-N' for filtering out some CANFD frames or '-f' for selecting a specific canif 

``` 
asc2log - convert ASC logfile to compact CAN frame logfile.
Usage: asc2log [-v][-t]
Options:
	-h         	display this help message
	-v         	increase verbosity
	-t         	fix time origin to 0
	-N <name> 	filter out frame names not starting with <name>
	-f <canif> 	filter out frames not from interface <canif>
	-i <infile>	(default stdin)
	-o <outfile>	(default stdout)
```
