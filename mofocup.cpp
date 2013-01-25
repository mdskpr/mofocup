#include <iostream>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include "bzfsAPI.h"
//#include "plugin_utils.h"
#include <map>

int processCallbackForRank(void *a_param, int argc, char **argv, char **column)
{
    int rank = atoi(argv[0]);

    return rank;
}

class mofocup : public bz_Plugin, public bz_CustomSlashCommandHandler
{
public:
    sqlite3* db; //sqlite database we'll be using
    std::string dbfilename; //the path to the database

    virtual const char* Name (){return "MoFo Cup";}
    virtual void Init(const char* /*commandLine*/);
    virtual void Cleanup(void);

    virtual void Event(bz_EventData *eventData);
    virtual bool SlashCommand(int playerID, bz_ApiString command, bz_ApiString message, bz_APIStringList *params);

    virtual void doQuery(std::string query);
    virtual void incrementCounter(std::string bzid, std::string cuptype, std::string incrementBy);
    virtual std::string convertToString(int myInt);
    virtual int determineRank(std::string bzID);

    struct playingTimeStructure
    {
        std::string bzid;
        double joinTime;
    };
    std::vector<playingTimeStructure> playingTime;
};

BZ_PLUGIN(mofocup);

void mofocup::Init(const char* commandLine)
{
    bz_registerCustomSlashCommand("cup", this); //register the /cup command

    if(commandLine == NULL || std::string(commandLine).empty()) //no database provided, unloadplugin ourselves
    {
        bz_debugMessage(0, "DEBUG :: MoFo Cup :: Please provide a filename for the database");
        bz_debugMessage(0, "DEBUG :: MoFo Cup :: -loadplugin /path/to/mofocup.so,/path/to/database.db");
        bz_debugMessage(0, "DEBUG :: MoFo Cup :: Unloading MoFoCup plugin...");
        bz_unloadPlugin(Name());
    }
    else
    {
        dbfilename = std::string(commandLine);
        bz_debugMessagef(0, "DEBUG :: MoFo Cup :: Using the following database: %s", dbfilename.c_str());
        sqlite3_open(dbfilename.c_str(),&db);

        if (db == 0) //we couldn't read the database provided
        {
            bz_debugMessagef(0, "DEBUG :: MoFo Cup :: Error! Could not connect to: %s", dbfilename.c_str());
            bz_debugMessage(0, "DEBUG :: MoFo Cup :: Unloading MoFoCup plugin...");
            bz_unloadPlugin(Name());
        }

        if (db != 0) //if the database is empty, let's create the tables needed
        {
            doQuery("CREATE TABLE Cups(CupID INTEGER, ServerID TEXT, StartTime TEXT, EndTime Text, CupType Text, primary key (CupID) )");
            doQuery("CREATE TABLE Captures(BZID INTEGER, CupID INTEGER, Counter INTEGER default (0), primary key(BZID,CupID) )");
        }

        if (!Register(bz_ePlayerDieEvent) || !Register(bz_eCaptureEvent) || !Register(bz_ePlayerPartEvent) || !Register(bz_ePlayerJoinEvent)) //unload the plugin if any events fail to register
        {
            bz_debugMessage(0, "DEBUG :: MoFo Cup :: A BZFS event failed to load.");
            bz_debugMessage(0, "DEBUG :: MoFo Cup :: Unloading MoFoCup plugin...");
            bz_unloadPlugin(Name());
        }
    }

    bz_debugMessage(4, "DEBUG :: MoFo Cup :: Successfully loaded and database connection ready.");
}

void mofocup::Cleanup()
{
    Flush();
    bz_removeCustomSlashCommand("cup");
    bz_removeCustomSlashCommand("rank");

    if (db != NULL) //close the database connection since we won't need it
      sqlite3_close(db);

    bz_debugMessage(4, "DEBUG :: MoFo Cup :: Successfully unloaded and database connection closed.");
}

void mofocup::Event(bz_EventData* eventData)
{
    switch (eventData->eventType)
    {
        case bz_eCaptureEvent: // A flag is captured
        {
	        int pre_rank;
	        int post_rank;
            bz_CTFCaptureEventData_V1* ctfdata = (bz_CTFCaptureEventData_V1*)eventData;


            // Members for bz_CTFCaptureEventData_V1:

            // bz_eTeamType teamCapped: The team capturing (winning)
            // bz_eTeamType teamCapping: The team being captured (losing)
            // int playerCapping: The player capturing the flag
            // float pos[3]: Player position
            // float rot: Direction the tank is facing
            // double eventTime: The game time (in seconds)

            bz_BasePlayerRecord *pr = bz_getPlayerByIndex(ctfdata->playerCapping);
            pre_rank = determineRank(pr->bzID.c_str());

            std::string capturerid = pr->bzID.c_str();
            incrementCounter(capturerid, "flag_capture", "1");

    	    post_rank = determineRank(pr->bzID.c_str());
    	    bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, eActionMessage, "Player: %s captured the flag! Earning 1 point.", pr->callsign.c_str());

    	    if(post_rank > pre_rank) {
                    bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, eActionMessage, "Player: %s is now rank %i", pr->callsign.c_str(), post_rank);
    	    }

            bz_freePlayerRecord(pr);
        }
        break;

        case bz_ePlayerDieEvent: // A player dies
        {
            bz_PlayerDieEventData_V1* diedata = (bz_PlayerDieEventData_V1*)eventData;

            // Members for bz_PlayerDieEventData_V1:

            // int playerID: The victim's ID
            // bz_eTeamType team: The victim's team
            // int killerID: The killer's ID
            // bz_eTeamType killerTeam: The killer's team
            // bz_ApiString flagKilledWith: The flag that the killer shot the victim with
            // int shotID: The shot ID of the bullet that killed the victim
            // int driverID: ID of the physics driver that killed the player, where applicable
            // bz_PlayerUpdateState state: The victim's state at the time of death
            // double eventTime: The game time (in seconds)

        }
        break;

        case bz_ePlayerJoinEvent: // A player joins
        {
            bz_PlayerJoinPartEventData_V1* joindata = (bz_PlayerJoinPartEventData_V1*)eventData;

            if(atoi(joindata->record->bzID.c_str()) > 0)
            {
                playingTimeStructure newPlayingTime;

                newPlayingTime.bzid = joindata->record->bzID.c_str();
                newPlayingTime.joinTime = bz_getCurrentTime();

                playingTime.push_back(newPlayingTime);
            }
        }
        break;

        case bz_ePlayerPartEvent: // A player parts
        {
            bz_PlayerJoinPartEventData_V1* partdata = (bz_PlayerJoinPartEventData_V1*)eventData;

            if (playingTime.size() > 0)
            {
                for (unsigned int i = 0; i < playingTime.size(); i++)
                {
                    if (playingTime.at(i).bzid == partdata->record->bzID.c_str())
                    {
                        std::string myBZID = partdata->record->bzID.c_str();
                        int timePlayed = bz_getCurrentTime() - playingTime.at(i).joinTime;
                        std::string updatePlayingTime = "INSERT OR REPLACE INTO `Captures` (BZID, CupID, PlayingTime) ";
                        updatePlayingTime += "VALUES ('" + myBZID + "', ";
                        updatePlayingTime += "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), ";
                        updatePlayingTime += "(SELECT COALESCE((SELECT `PlayingTime` + " + convertToString(timePlayed) + " FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + myBZID + "' AND `Captures`.`CupID` = `Cups`.`CupID` and `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 1)))";

                        doQuery(updatePlayingTime);
                        playingTime.erase(playingTime.begin() + i, playingTime.begin() + i + 1);
                    }
                }
            }
        }
        break;

        default:
        break;
    }
}

bool mofocup::SlashCommand(int playerID, bz_ApiString command, bz_ApiString message, bz_APIStringList *params)
{
    if(command == "cup")
    {
        // Handle the command "cup" here.
    } else if(command == "rank") {
      //Handle rank command.

    }
}

std::string mofocup::convertToString(int myInt)
{
    std::string myString;
    std::stringstream string;
    string << myInt; //Use an stringstream to pass an int through a string
    myString = string.str();

    return myString;
}

int mofocup::determineRank(std::string bzID)
{
    char* db_err = 0;
    std::string query = "SELECT (SELECT COUNT(*) FROM `captures` AS c2 WHERE c2.Counter > c1.Counter) + 1 AS row_Num FROM `Captures` AS c1 WHERE `BZID` = '" + bzID + "' ORDER BY Counter DESC";
    int ret = sqlite3_exec(db, query.c_str(), processCallbackForRank, 0, &db_err);
}

void mofocup::doQuery(std::string query)
{
    bz_debugMessage(2, "DEBUG :: MoFo Cup :: Executing following SQL query...");
    bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", query.c_str());

    char* db_err = 0;
    int ret = sqlite3_exec(db, query.c_str(), NULL, 0, &db_err);

    if (db_err != 0)
    {
        bz_debugMessage(2, "DEBUG :: MoFo Cup :: SQL ERROR!");
        bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", db_err);
    }
}

void mofocup::incrementCounter(std::string bzid, std::string cuptype, std::string incrementBy)
{
    char* db_err = 0;

    std::string query = "INSERT OR REPLACE INTO `Captures` (BZID, CupID, Counter) ";
    query += "VALUES ('" + bzid + "', ";
    query += "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), ";
    query += "(SELECT COALESCE((SELECT `Counter` + 1 FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + bzid + "' AND `Captures`.`CupID` = `Cups`.`CupID` and `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 1)))";

    doQuery(query);
}
