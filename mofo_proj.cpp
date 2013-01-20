#include "mofo_proj.h"

typedef 
{
 int player_bzID;
 int cap_num;
 string player_name;
}ScorerDef;

class CapNRecEvents : public bz_Plugin
{
  public:
       virtual void Init(const char*);
       virtual void Event(bz_eventData);
}

virtual void Init(const char*)
{
     Register(bz_eCaptureEvent);
}


virtual void Event(bz_eventData *eventData)
{
	ScorerDef capper;
	ofstream data_file;
	data_file = fopen("cap_record.txt", "w+");
	int lineno;
	switch(eventData->eventType){
	    case bz_eCaptureEvent:
	       bz_CaptureEventData_V1* data = (bz_CaptureEventData_V1*)eventData;
	       bz_BasePlayerRecord *player = bz_getPlayerByIndex(data.playerCapping);
	       capper.player_bzID = player.bzID;
	       if(doesPlayerDataExist(capper.player_bzID, data_file)){
	          string capper_line;
	          capper.cap_num++;
		  while(!data_file.eof()) {
 		   string sLine = ";";
		   string each_line
		   eache_line = getLine(data_file
		  }
		

		  
	       
	       } else {
	          capper.cap_num++;
	          data_file  << capper.player_bzID << ";" << "\0" << capper.cap_num << "\n";

	       }

 	       

	}
	

}

bool doesPlayerDataExist(int player_bzID, ofstream data_file){
    while(!data_file.eof()) {
       string sLine = ";";
       string each_line;
       each_line = getline(data_file, sLine);
       if(strcmp(player_bzid, each_line) == true){
        return true;
       }
    }
    return false;
}


BZ_PLUGIN(CapNRecEvents);

