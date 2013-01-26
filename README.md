# Planet MoFo Cup

This is the BZFlag plug-in used by Planet MoFo for their monthly cups to see who has captured the flag the most, who has had the most geno kills, and more tournamets.

## Compiling
Add this plug-in to the BZFlag build system and compile.

    sh newplug.sh teamSwitch
    cd teamSwitch
    make
    sudo make install

## Setup

```
bzfs -loadplugin /path/to/mofoup.so,/path/to/mofocup.sqlite
```

### Slash Commands

```
    /cup
    /rank
```

## Notes

This plug-in is for the exclusive use of Planet MoFo only.

## License

BSD