#include "TRX.h"

const struct _Bands Bands[BAND_COUNT] = {
  DEFINED_BANDS
};

TRX::TRX() {
  for (byte i=0; i < BAND_COUNT; i++) {
	  BandData[i].VFO_Index = 0;
	  if (Bands[i].startSSB != 0)
	    BandData[i].VFO[0] = BandData[i].VFO[1] = Bands[i].startSSB;
	  else
	    BandData[i].VFO[0] = BandData[i].VFO[1] = Bands[i].start;
	  BandData[i].sideband = Bands[i].sideband;
	  BandData[i].AttPre = 0;
	  BandData[i].Split = false;
  }
  Lock = RIT = TX = QRP = false;
  RIT_Value = 0;
  BandData[BAND_COUNT].VFO[0] = 0;
  SwitchToBand(BAND_COUNT == 1 ? 0 : 1);
}

uint16_t hash_data(uint16_t hval, uint8_t* data, int sz) {
  while (sz--)
    hval += (hval << 5) + *data++;
  return hval;
}  

uint16_t TRX::StateHash() {
  uint16_t hval = 5381;
  hval = hash_data(hval, (uint8_t*)BandData, sizeof(BandData));
  hval = hash_data(hval, (uint8_t*)&BandIndex, sizeof(BandIndex));
  hval = hash_data(hval, (uint8_t*)&state, sizeof(state));
  return hval;
}

#define STATE_SIGN  0x59CE

void TRX::StateLoad(Eeprom24C32 &eep) {
  uint16_t sign=0,addr=0;
  eep.readBytes(addr,sizeof(sign),(byte*)&sign);
  if (sign == STATE_SIGN) {
    addr += sizeof(sign);
    eep.readBytes(addr,sizeof(BandData),(byte*)BandData);
    addr += sizeof(BandData);   
    eep.readBytes(addr,sizeof(SaveBandIndex),(byte*)&SaveBandIndex);
    addr += sizeof(SaveBandIndex);
    eep.readBytes(addr,sizeof(BandIndex),(byte*)&BandIndex);
    addr += sizeof(BandIndex);
    eep.readBytes(addr,sizeof(state),(byte*)&state);
  }
}

void TRX::StateSave(Eeprom24C32 &eep) {
  uint16_t sign=STATE_SIGN,addr=0;
  eep.writeBytes(addr,sizeof(sign),(byte*)&sign);
  addr += sizeof(sign);
  eep.writeBytes(addr,sizeof(BandData),(byte*)BandData);
  addr += sizeof(BandData);
  eep.writeBytes(addr,sizeof(SaveBandIndex),(byte*)&SaveBandIndex);
  addr += sizeof(SaveBandIndex);
  eep.writeBytes(addr,sizeof(BandIndex),(byte*)&BandIndex);
  addr += sizeof(BandIndex);
  eep.writeBytes(addr,sizeof(state),(byte*)&state);
}

void TRX::SwitchToBand(int band) {
  SaveBandIndex = BandIndex = band;
  memcpy(&state,&BandData[BandIndex],sizeof(TVFOState));
  Lock = RIT = false;
  RIT_Value = 0;
}

void TRX::ChangeFreq(long freq_delta) {
  if (!TX && !Lock) {
    state.VFO[state.VFO_Index] += freq_delta;
    // проверяем выход за пределы диапазона
    if (BandIndex >= 0) {
      if (state.VFO[state.VFO_Index] < Bands[BandIndex].start)
        state.VFO[state.VFO_Index] = Bands[BandIndex].start;
      else if (state.VFO[state.VFO_Index] > Bands[BandIndex].end)
        state.VFO[state.VFO_Index] = Bands[BandIndex].end;
    } else {
      if (state.VFO[state.VFO_Index] < FREQ_MIN)
        state.VFO[state.VFO_Index] = FREQ_MIN;
      else if (state.VFO[state.VFO_Index] > FREQ_MAX)
        state.VFO[state.VFO_Index] = FREQ_MAX;
    }
  }
}

void TRX::ExecCommand(uint8_t cmd) {
  if (TX && (cmd != cmdQRP)) return; // блокируем при передаче
  switch (cmd) {
    case cmdAttPre: // переключает по кругу аттенюатор/увч
      if (++state.AttPre > 2) state.AttPre = 0;
      break;
    case cmdVFOSel: // VFO A/B
      state.VFO_Index ^= 1;
      break;
    case cmdUSBLSB:
      state.sideband = (state.sideband == LSB ? USB : LSB);
      break;
    case cmdVFOEQ:
      state.VFO[state.VFO_Index ^ 1] = state.VFO[state.VFO_Index];
      break;
    case cmdQRP:
      QRP = !QRP;
      break;
    case cmdLock:
      Lock = !Lock;
      break;
    case cmdSplit:
      state.Split = !state.Split;
      break;
    case cmdZero:
      state.VFO[state.VFO_Index] = ((state.VFO[state.VFO_Index]+500) / 1000)*1000;
      break;
    case cmdRIT:
      RIT = !RIT;
      break;
    case cmdBandUp:
	    if (BandIndex >= 0) {
		    memcpy(&BandData[BandIndex],&state,sizeof(TVFOState));
        if (++BandIndex >= BAND_COUNT)
          BandIndex = 0;
        memcpy(&state,&BandData[BandIndex],sizeof(TVFOState));
        Lock = RIT = false;
      } else {
        if ((state.VFO[state.VFO_Index]+=1000000) > FREQ_MAX)
          state.VFO[state.VFO_Index] = FREQ_MAX;
      }
      break;
    case cmdBandDown:
      if (BandIndex >= 0) {
        memcpy(&BandData[BandIndex],&state,sizeof(TVFOState));
		    if (--BandIndex < 0)
          BandIndex = BAND_COUNT-1;
        memcpy(&state,&BandData[BandIndex],sizeof(TVFOState));
        Lock = RIT = false;
      } else {
        if ((state.VFO[state.VFO_Index]-=1000000) < FREQ_MIN)
          state.VFO[state.VFO_Index] = FREQ_MIN;
      }
      break;
    case cmdHam:
      if (BandIndex >= 0) {
        memcpy(&BandData[BandIndex],&state,sizeof(TVFOState));
        SaveBandIndex = BandIndex;
        BandIndex = -1;
        if (BandData[BAND_COUNT].VFO[0] != 0)
          memcpy(&state,&BandData[BAND_COUNT],sizeof(TVFOState));
      } else {
        memcpy(&BandData[BAND_COUNT],&state,sizeof(TVFOState));
        BandIndex = SaveBandIndex;
        memcpy(&state,&BandData[BandIndex],sizeof(TVFOState));
      }
      Lock = RIT = false;
      break;
  }
}

uint8_t TRX::inCW() {
  uint8_t vfo_idx = GetVFOIndex();
  return 
    BandIndex >= 0 && Bands[BandIndex].startSSB > 0 &&
    state.VFO[vfo_idx] < Bands[BandIndex].startSSB &&
    state.VFO[vfo_idx] >= Bands[BandIndex].start;
}

