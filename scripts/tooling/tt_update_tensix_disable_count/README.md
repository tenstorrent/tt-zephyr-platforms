# Tenstorrent Tensix Harvesting Tool

This tool is intended to support editing the count of disabled Tensix
columns within a firmware bundle for Tenstorrent Blackhole cards. The primary
use case for this tool is enabling Tensix cores that have been "soft harvested"
on p150 cards. p150 cards (purchased prior to Jan 2026) contain 20 additional
Tensix cores, which can be reenabled with this tool. Note that metal and other
Tenstorrent software will not optimize for these cores moving forward.

The Tensix column count is set by specification tables in the firmware bundle,
which this tool is capable of editing. A modified firmware bundle can then be
flashed to the Blackhole card in order to enable additional Tensix columns.

## Requirements

This tool requires `protoc` be installed and available on your path. On Ubuntu,
this can be installed with the command `sudo apt install protobuf-compiler`

## Usage

In order to modify the Tensix disable count for all Blackhole p150 cards, run
the following. Please use tt-flash >= 3.6.0 to avoid known issues with
overwriting board configuration data:

```
pip install tt-update-tensix-disable-count
tt-update-tensix-disable-count \
	--input <input_fwbundle> \
	--output <output_fwbundle>  \
	--board P150A-1 --board P150B-1 --board P150C-1 \
	--disable-count 0
pip install tt-flash>=3.6.0
tt-flash <output_fwbundle>
# Verify number of Tensix cores available
python3 -c "import pyluwen; print(pyluwen.detect_chips()[0].get_telemetry().tensix_enabled_col.bit_count() * 10)"
```


## Support

While Tenstorrent will continue to support this tool, Metal and other
Tenstorrent software will not optimize for the additional 20 Tensix
cores on early p150 cards going forwards.
