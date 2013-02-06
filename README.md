# Planet MoFo Cup

This is the BZFlag plug-in used by Planet MoFo for their monthly cups to see who has captured the flag the most, who has had the most geno kills, and other tournamets.

## Compiling
Add this plug-in to the BZFlag build system and compile.
```
sh newplug.sh mofocup
cd mofocup
make
sudo make install
```

## Setup

```
bzfs -loadplugin /path/to/mofoup.so,/path/to/mofocup.sqlite
```

### Slash Commands

```
/cup <bounty | ctf | geno>
/rank
```
* The `/cup` command will show you the top 10 players of the responding cups.
* The `/rank` command will display your current position in all the available tournaments.

## Formulas
To calculate the amount of points gained for each capture, we use the following formula:
```
8 * (numberOfPlayersOnCappedTeam - numberOfPlayersOnCappingTeam) + 3 * (numberOfPlayersOnCappedTeam)
```

To calculate the amount of points a player has in the current ctf cup, we use the following formula:
```
(Total of Cap Points) / (Total Seconds Played / 86400)
```

## Notes

This plug-in is for the exclusive use of Planet MoFo only.

## License

BSD
