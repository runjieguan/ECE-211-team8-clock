#ifndef AUDIO_W_TIMER // overarching ifdef guard here for the whole file!
#define AUDIO_W_TIMER


int64_t getRTCtime(); 
void sendCommand(uint8_t command, uint16_t parameter); 
void playAudio(); 
void selectTrack(int track); 
void stopAudio(); 
void changeVolume(int amount); 
void stopSystem(); 
void checkSerialInput(); 
void setupAudio(); 
void timerRun(); 


#endif 
