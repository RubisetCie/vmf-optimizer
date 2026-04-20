# VMF Optimizer

Program to compress Valve Map Files (VMF) for archival purposes, compatible with any Source Engine game.

## Processing

The program will remove parameters that have default values. When the modified file is opened, Hammer will fill the missing parameters with default values; nothing changes, hence lossless optimization.

Since saving the file will re-add the default values again, the tool is best used for archival purposes. (if you're like me and have a lot of vmfs of your previous map versions, this tool will be useful)

## Usage

Run the following command-line:

```
vmfoptimizer (options) map-1.vmf [map-2.vmf […vmf…]]
```

## Options

The program supports the following options:

```
-h|--help         : Show help.
-i|--in-place     : Replace the original file.
-l|--log (file)   : Save the output to a separate log file.
-o|--output (dir) : Put all outputs to a specific directory.
-p|--prefab       : Erase editor-specific informations (intended for prefabs).
--skip-solids     : Skip solid processing.
--skip-defaults   : Skip removing entity parameters with default values.
--remove-comment  : Remove the map comment.
--keep-vert-plus  : Keep vertices plus informations (Hammer++ specific).
--keep-whitespace : Do not strip whitespaces to output lines (indentation, etc).
-c|-r|--carriages : Output with carriage returns in addition to line feeds.
-v|--verbose      : More verbose output.
```

## Building

Compilation from source can be done using GNU Make:

```
make
```

## Install

To install, run the following target:

```
make install PREFIX=(prefix)
```

The variable `PREFIX` defaults to `/usr/local`.

## Uninstall

To uninstall, run the following target using the same prefix as specified in the install process:

```
make uninstall PREFIX=(prefix)
```

## Credits

Original by [dabmasterars](https://github.com/dabmasterars/VMF-Optimizer)
