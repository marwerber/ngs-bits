### BedShrink tool help
	BedShrink (0.1-222-g9be2128)
	
	Shrinks the regions in a BED file.
	
	Mandatory parameters:
	  -n <int>     The number of bases to shrink (on both sides of each region).
	
	Optional parameters:
	  -in <file>   Input BED file. If unset, reads from STDIN.
	               Default value: ''
	  -out <file>  Output BED file. If unset, writes to STDOUT.
	               Default value: ''
	
	Special parameters:
	  --help       Shows this help and exits.
	  --version    Prints version and exits.
	  --changelog  Prints changeloge and exits.
	  --tdx        Writes a Tool Definition Xml file. The file name is the application name with the suffix '.tdx'.
	
### BedShrink changelog
	BedShrink 0.1-222-g9be2128
	
[back to ngs-bits](https://github.com/marc-sturm/ngs-bits)