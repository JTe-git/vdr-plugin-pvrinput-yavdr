#include "common.h"
#include <linux/dvb/video.h>
#include <stdlib.h>

#if VDRVERSNUM > 10712
#define PVR_SOURCEPARAMS
#include <vdr/sourceparams.h>
#endif

char DRIVERNAME[][15] = {
  "undef", "ivtv", "cx18", "pvrusb2", "cx88_blackbird", "hdpvr"
};

char CARDNAME[][9] = {
  "undef", "PVR150", "PVR250", "PVR350", "PVR500#1", "PVR500#2", "HVR1300", "HVR1600", "HVR1900", "HVR1950", "PVRUSB2", "HDPVR"
};

cPvrDevice * PvrDevices[kMaxPvrDevices];

cString cPvrDevice::externChannelSwitchScript;

#ifdef PVR_SOURCEPARAMS
class cPvrSourceParam : public cSourceParam {
private:
  int param;
  int input;
  int standard;
  int card;
  cChannel data;
public:
  cPvrSourceParam();
  cString ParametersToString(void) const;
  void    ParseParameters(const char*  Parameters);
  virtual void SetData(cChannel *Channel);
  virtual void GetData(cChannel *Channel);
  virtual cOsdItem *GetOsdItem(void);

  static bool IsPvr(int Code);
  
  static const char  sSourceID = 'V';
  static const uint  stPvr = (sSourceID << 24);
  static const int   sNumInputs = 12;
  static const char* sInputs[];
  static const int   sNumCards = 9;
  static const char* sCards[];
  static const int   sNumStandards = 31;
  static const char* sStandards[];
};

const char* cPvrSourceParam::sInputs[] = {
 "TV",
 "RADIO",
 "COMPOSITE0",
 "COMPOSITE1",
 "COMPOSITE2",
 "COMPOSITE3",
 "COMPOSITE4",
 "SVIDEO0",
 "SVIDEO1",
 "SVIDEO2",
 "SVIDEO3",
 "COMPONENT"
};

const char* cPvrSourceParam::sCards[] = {
 "",
 "CARD0",
 "CARD1",
 "CARD2",
 "CARD3",
 "CARD4",
 "CARD5",
 "CARD6",
 "CARD7"
};

const char* cPvrSourceParam::sStandards[] = {
  "",
  "PAL",
  "NTSC",
  "SECAM",
  "NTSC_M",
  "NTSC_M_JP",
  "NTSC_443",
  "NTSC_M_KR",
  "PAL_M",
  "PAL_N",
  "PAL_NC",
  "PAL_B",
  "PAL_B1",
  "PAL_G",
  "PAL_BG",
  "PAL_D",
  "PAL_D1",
  "PAL_K",
  "PAL_DK",
  "PAL_H",
  "PAL_I",
  "PAL_60",
  "SECAM_B",
  "SECAM_D",
  "SECAM_G",
  "SECAM_H",
  "SECAM_K",
  "SECAM_K1",
  "SECAM_DK",
  "SECAM_L",
  "SECAM_LC"
};

cPvrSourceParam::cPvrSourceParam()
:cSourceParam(sSourceID, "analog (pvrinput)")
{
  param = 0;
  input = 0;
  standard = 0;
  card = 0;
}

cString cPvrSourceParam::ParametersToString(void) const
{
  if ((standard == 0) && (card == 0))
    return cString::sprintf("%s", sInputs[input]);
  if ((standard == 0) && (card != 0))
    return cString::sprintf("%s|%s", sInputs[input], sCards[card]);
  if ((standard != 0) && (card == 0))
    return cString::sprintf("%s|%s", sInputs[input], sStandards[standard]);
  return cString::sprintf("%s|%s|%s", sInputs[input], sCards[card], sStandards[standard]);
}

void    cPvrSourceParam::ParseParameters(const char*  Parameters) {
  char * Input     = NULL;
  char * optArg[2] = { NULL, NULL };
  int NumArgs = sscanf(Parameters, "%a[^|]|%a[^|]|%a[^:\n]", &Input, &optArg[0], &optArg[1]);
  input = 0;
  card = 0;
  standard = 0;
  if (NumArgs >= 1) {
    for (int i = 0; sInputs[i]; i++) {
      if (!strcasecmp (Input, sInputs[i])) {
        input = i;
        break;
      }
    }
  }
  for (int opt = 0; opt < 2; opt++) {
    if (NumArgs >= (opt + 2)) {
      bool parsed = false;
      for (int c = 1; sCards[c]; c++) {
        if (!strcasecmp (optArg[opt], sCards[c])) {
          card = c;
          parsed = true;
          break;
        }
      }
      if (!parsed) {
        for (int s = 1; sStandards[s]; s++) {
          if (!strcasecmp (optArg[opt], sStandards[s])) {
            standard = s;
            break;
          }
        }
      }
    }
  }
}

void cPvrSourceParam::SetData(cChannel *Channel)
{
  data = *Channel;
  ParseParameters(data.Parameters());
  param = 0;
}

void cPvrSourceParam::GetData(cChannel *Channel)
{
  data.SetTransponderData(Channel->Source(), Channel->Frequency(), Channel->Srate(), ParametersToString(), true);
  *Channel = data;
}

cOsdItem *cPvrSourceParam::GetOsdItem(void)
{
  switch (param++) {
    case 0: return new cMenuEditStraItem(tr("SourceParam.pvrinput$Input"), &input, sNumInputs, sInputs);
    case 1: return new cMenuEditStraItem(tr("SourceParam.pvrinput$Card"), &card, sNumCards, sCards);
    case 2: return new cMenuEditStraItem(tr("SourceParam.pvrinput$Standard"), &standard, sNumStandards, sStandards);
    default: return NULL;
  }
  return NULL;
}

bool cPvrSourceParam::IsPvr(int Code) {
  return (Code & cSource::st_Mask) == stPvr;
}
#endif

static void cPvrSourceParam_Initialize() {
#ifdef PVR_SOURCEPARAMS
  new cPvrSourceParam();
#endif
}

cPvrDevice::cPvrDevice(int DeviceNumber)
:  number(DeviceNumber),
  frequency(-1),
  CurrentNorm(0), //uint64_t can't be negative
  CurrentLinesPerFrame(-1),
  CurrentInput(-1),
  SupportsSlicedVBI(false),
  hasDecoder(false),
  hasTuner(true),
  streamType(0),
  dvrOpen(false),
  delivered(false),
  isClosing(false),
  readThreadRunning(false),
  ChannelSettingsDone(false),
  FirstChannelSwitch(true),
  pvrusb2_ready(true),
  driver(undef),
  cardname(UNDEF),
  tsBufferPrefill(0),
  readThread(0)
{ log(pvrDEBUG2,"new cPvrDevice (%d)", number);
  v4l2_fd = mpeg_fd = radio_fd = -1;
  v4l2_dev = mpeg_dev = radio_dev = -1;
  vpid = apid = tpid = -1;
  cString devName;
  struct v4l2_capability video_vcap;
  struct v4l2_capability capability;
  struct v4l2_input input;
  int i;
  // put all values here that are set only *once*
  v4l2_dev = number; 
  devName = cString::sprintf("/dev/video%d", v4l2_dev);
  v4l2_fd = open(devName, O_RDWR);
  if (v4l2_fd < 0) {
    log(pvrERROR, "cPvrDevice::cPvrDevice(): error opening video device %s: %s",
      *devName, strerror(errno));
    }
  memset(&video_vcap, 0, sizeof(video_vcap));
  IOCTL(v4l2_fd, VIDIOC_QUERYCAP, &video_vcap);
  log(pvrDEBUG1, "%s = %s", *devName, video_vcap.card);
  BusID = cString::sprintf("%s", video_vcap.bus_info);
  log(pvrDEBUG1, "BusID = %s", *BusID);
  if       (!memcmp(video_vcap.driver, "ivtv",4))                   driver = ivtv;
  else if  (!memcmp(video_vcap.driver, "cx18",4))                   driver = cx18;
  else if  (!memcmp(video_vcap.driver, "pvrusb2",7))                driver = pvrusb2;
  else if  (!memcmp(video_vcap.driver, "cx88_blackbird",14))        driver = cx88_blackbird;
  else if  (!memcmp(video_vcap.driver, "hdpvr",5))                  driver = hdpvr;
  else { driver = undef; log(pvrDEBUG1, "unknown video driver: \"%s\"",video_vcap.driver); }
  if       (!memcmp(video_vcap.card, "Hauppauge WinTV PVR-150",23)) cardname = PVR150;
  else if  (!memcmp(video_vcap.card, "Hauppauge WinTV PVR-250",23)) cardname = PVR250;
  else if  (!memcmp(video_vcap.card, "Hauppauge WinTV PVR-350",23)) cardname = PVR350;
  else if  (!memcmp(video_vcap.card, "WinTV PVR 500 (unit #1)",23)) cardname = PVR500_1;
  else if  (!memcmp(video_vcap.card, "WinTV PVR 500 (unit #2)",23)) cardname = PVR500_2;
  else if  (!memcmp(video_vcap.card, "Hauppauge WinTV-HVR1300",23)) cardname = HVR1300;
  else if  (!memcmp(video_vcap.card, "Hauppauge HVR-1600",18))      cardname = HVR1600;
  else if  (!memcmp(video_vcap.card, "WinTV HVR-1900",14))          cardname = HVR1900;
  else if  (!memcmp(video_vcap.card, "WinTV HVR-1950",14))          cardname = HVR1950;
  else if  (!memcmp(video_vcap.card, "WinTV PVR USB2",14))          cardname = PVRUSB2;
  else if  (!memcmp(video_vcap.card, "Hauppauge HD PVR",16))        cardname = HDPVR;
  else if  (!memcmp(video_vcap.card, "Haupauge HD PVR",15))         cardname = HDPVR; // spelling error in hdpvr
  else { cardname = UNDEF; log(pvrDEBUG1, "unknown video card: \"%s\"",video_vcap.card); }
  if (cardname == HVR1300) log(pvrERROR, "HVR 1300 is not really supported yet");
  driver_apiversion = video_vcap.version;
  log(pvrDEBUG1, "%s Version 0x%06x", DRIVERNAME[driver], driver_apiversion);
  if ((video_vcap.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) && (driver != pvrusb2)) {
    /*The pvrusb2 driver advertises vbi capability, although it isn't there.
      This was fixed in v4l-dvb hg in 01/2009 and will hopefully be in Kernel 2.6.30*/
    SupportsSlicedVBI = true;
    log(pvrDEBUG1, "%s supports sliced VBI Capture",*devName);
    }
  if (video_vcap.capabilities & V4L2_CAP_VIDEO_OUTPUT_OVERLAY) {
    hasDecoder = true; //can only be a PVR350
    }
  for (i=0; i<kMaxPvrDevices; i++) {
    if (radio_dev<0 && (video_vcap.capabilities & V4L2_CAP_RADIO)) { //searching matching radio dev
      devName = cString::sprintf("/dev/radio%d", i);
      radio_fd = open(devName, O_RDWR);
      if (radio_fd >= 0) {
        memset(&capability, 0, sizeof(capability));
        if (IOCTL(radio_fd, VIDIOC_QUERYCAP, &capability) !=0) {
          log(pvrERROR, "VIDIOC_QUERYCAP failed, %d:%s",
            errno, strerror(errno));
          }
        if (!strncmp(*BusID,(const char *)  capability.bus_info, strlen(*BusID)-1)) {
          radio_dev=i; // store info for later
          log(pvrDEBUG1, "/dev/radio%d = FM radio dev",radio_dev);
          }
        close(radio_fd); // a pvrusb2 will remain on input 3. The bool FirstChannelSwitch will solve this later
        radio_fd = -1;
      }
    }
    if (mpeg_dev<0 && (driver == cx88_blackbird)) { // the blackbird uses two (!) different devices, search the other one.
      close(v4l2_fd);
      v4l2_fd = -1;
      devName = cString::sprintf("/dev/video%d", i);
      mpeg_fd = open(devName, O_RDWR);
      if (mpeg_fd) {
        memset(&capability, 0, sizeof(capability));
        IOCTL(mpeg_fd, VIDIOC_QUERYCAP, &capability);
        if (!strncmp(*BusID ,(const char *) capability.bus_info, strlen(*BusID)-1) &&
            !strcmp("cx8800",(const char *)  capability.driver)) {
          mpeg_dev = v4l2_dev; //for this driver we found mpeg_dev up to now.
          v4l2_dev = i;        //reassigning, now with correct value.
          log(pvrDEBUG1, "/dev/video%d = v4l2 dev (analog properties: volume/hue/brightness/inputs..)",v4l2_dev);
          log(pvrDEBUG1, "/dev/video%d = mpeg dev (MPEG properties: bitrates/frame rate/filters..)",mpeg_dev);
          }
        close(mpeg_fd);
        mpeg_fd = -1;
        }
      }
    } // end device search loop
  switch (driver) {
    case ivtv:        //ivtv, cx18, pvrusb2 and hdpvr share the same device.
    case cx18:
    case pvrusb2:
    case hdpvr:
          mpeg_dev = v4l2_dev;
          mpeg_fd  = v4l2_fd;
          break;
    case cx88_blackbird:
                      //blackbird: reopen mpeg device.               //FIXME: WE SHOULD OPEN V4L2 DEV for inputs/picture properties/volume, mpeg for all other stuff 
          devName = cString::sprintf("/dev/video%d", mpeg_dev);
          v4l2_fd = open(devName, O_RDWR);                           //FIXME: FOR NOW THIS IS A WILD MIXTURE.
          break;
    default:;
    }
  QueryAllControls(); //we have to split in mpeg and v4l2 here.
  memset(&inputs, -1, sizeof(inputs)); 
  numInputs = 0;
  for (i = 0;; i++) {
    memset(&input, 0, sizeof(input));
    input.index = i;
    if (IOCTL(v4l2_fd, VIDIOC_ENUMINPUT, &input) == -1) {
      break;
      }
    else {
      log(pvrDEBUG1, "input %d = %s", i, input.name);
      numInputs++;
      }
    if      (!memcmp(input.name, "Tuner",        5)) { inputs[eTelevision]=inputs[eRadio]=i; continue; } //ivtv:   Radio and TV tuner are same input.
    else if (!memcmp(input.name, "television",  10)) { inputs[eTelevision]=i; continue; }                //pvrusb2
    else if (!memcmp(input.name, "Television",  10)) { inputs[eTelevision]=i; continue; }                //cx88_blackbird
    else if (!memcmp(input.name, "radio",        5)) { inputs[eRadio]     =i; continue; }                //pvrusb2
    else if (!memcmp(input.name, "Composite 0", 11)) { inputs[eComposite0]=i; continue; }
    else if (!memcmp(input.name, "Composite 1", 11)) { inputs[eComposite1]=i; continue; }
    else if (!memcmp(input.name, "Composite 2", 11)) { inputs[eComposite2]=i; continue; }
    else if (!memcmp(input.name, "Composite 3", 11)) { inputs[eComposite3]=i; continue; }
    else if (!memcmp(input.name, "Composite 4", 11)) { inputs[eComposite4]=i; continue; }
    else if (!memcmp(input.name, "Composite1",  10)) { inputs[eComposite1]=i; continue; }                //cx88_blackbird
    else if (!memcmp(input.name, "composite",    9)) { inputs[eComposite0]=i; continue; }                //pvrusb2
    else if (!memcmp(input.name, "Composite",    9)) { inputs[eComposite0]=i; continue; }                //hdpvr
    else if (!memcmp(input.name, "S-Video 3",    9)) { inputs[eSVideo3]   =i; continue; }
    else if (!memcmp(input.name, "S-Video 0",    9)) { inputs[eSVideo0]   =i; continue; }
    else if (!memcmp(input.name, "S-Video 1",    9)) { inputs[eSVideo1]   =i; continue; }
    else if (!memcmp(input.name, "S-Video 2",    9)) { inputs[eSVideo2]   =i; continue; }
    else if (!memcmp(input.name, "S-Video",      7)) { inputs[eSVideo0]   =i; continue; }                //cx88_blackbird & hdpvr
    else if (!memcmp(input.name, "s-video",      7)) { inputs[eSVideo0]   =i; continue; }                //pvrusb2
    else if (!memcmp(input.name, "Component",    9)) { inputs[eComponent] =i; continue; }                //hdpvr
    else log(pvrERROR, "unknown input %s. PLEASE SEND LOG TO MAINTAINER.", input.name);
    }
  if (inputs[eTelevision] >= 0)
    SetInput(inputs[eTelevision]);
  else if ((driver == hdpvr) && (inputs[eComponent] >= 0)) {
    hasTuner = false;
    SetInput(inputs[eComponent]);
    }
  else {
    hasTuner = false;
    log(pvrERROR, "device has no tuner");
    }
  tsBuffer = new cRingBufferLinear(MEGABYTE(PvrSetup.TsBufferSizeMB), TS_SIZE, false, "PVRTS");
  tsBuffer->SetTimeouts(100, 100);
  ResetBuffering();
  GetStandard();
  if (driver == hdpvr) {
    SetControlValue(&PvrSetup.HDPVR_AudioEncoding, PvrSetup.HDPVR_AudioEncoding.value);
    SetAudioInput(PvrSetup.HDPVR_AudioInput);
    }
  else {
    SetControlValue(&PvrSetup.AudioEncoding, V4L2_MPEG_AUDIO_ENCODING_LAYER_2);
    SetControlValue(&PvrSetup.VideoBitratePeak, 15000000);
    SetVideoSize(720, CurrentLinesPerFrame == 525 ? 480 : 576);
    /* the driver will later automatically adjust the height depending on standard changes */ 
    }
  ReInit();
  StartSectionHandler();
}

cPvrDevice::~cPvrDevice() {
#if VDRVERSNUM >= 10600
  StopSectionHandler();
#endif
  cRingBufferLinear * tsBuffer_tmp = tsBuffer;
  log(pvrDEBUG2,"~cPvrDevice()");
  tsBuffer = NULL;
  delete tsBuffer_tmp;
  close(radio_fd);
  close(v4l2_fd);
}

bool cPvrDevice::Probe(int DeviceNumber) {
  char device[256];
  struct v4l2_capability vcap;
  struct v4l2_format vfmt;
  int v4l2_fd;
  bool found = false;
  memset(&vcap, 0, sizeof(vcap));
  memset(&vfmt, 0, sizeof(vfmt));
  sprintf(device, "/dev/video%d", DeviceNumber);
  v4l2_fd = open(device, O_RDONLY);
  if (v4l2_fd >=0) {
    IOCTL(v4l2_fd, VIDIOC_QUERYCAP, &vcap);
    if (!memcmp(vcap.driver, "ivtv",4) ||
        !memcmp(vcap.driver, "cx18",4) ||
        !memcmp(vcap.driver, "pvrusb2", 7) ||
        !memcmp(vcap.driver, "hdpvr", 5))
       found = true;
    if (!memcmp(vcap.driver, "cx88_blackbird", 14)) {
      vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (! IOCTL(v4l2_fd, VIDIOC_G_FMT, &vfmt) &&
         (v4l2_fourcc('M','P','E','G') == vfmt.fmt.pix.pixelformat))      
          found=true;
       }
    close(v4l2_fd);
    if (found) log(pvrINFO, "cPvrDevice::Probe():found %s", vcap.card);
    return found;
    }
  return false;
}

bool cPvrDevice::Initialize(void) {
  int found = 0;
  cPvrSourceParam_Initialize();
  for (int i = 0; i < kMaxPvrDevices; i++) {
    PvrDevices[i] = NULL;
    if (Probe(i)) {
      PvrDevices[i] = new cPvrDevice(i);
      found++;
      }
    }
  if (found)
    log(pvrINFO, "cPvrDevice::Initialize(): found %d PVR device%s", found, found > 1 ? "s" : "");
  else
    log(pvrINFO, "cPvrDevice::Initialize(): no PVR device found");
  externChannelSwitchScript = AddDirectory(cPlugin::ConfigDirectory(PLUGIN_NAME_I18N), "externchannelswitch.sh");
  return found > 0;
}

void cPvrDevice::Stop(void) {
  /* recursively stop all threads inside pvrinputs devices */
  for (int i = 0; i < kMaxPvrDevices; i++) {
    if (PvrDevices[i]) {
      if (PvrDevices[i]->readThread) {
        log(pvrDEBUG2,"cPvrDevice::Stop() for Device %i", i);
        PvrDevices[i]->StopReadThread();
        PvrDevices[i]->SetEncoderState(eStop);
        PvrDevices[i]->SetVBImode(PvrDevices[i]->CurrentLinesPerFrame, 0);
        }
      }
    }
}

void cPvrDevice::GetStandard(void) {
  v4l2_std_id std;
  if (IOCTL(v4l2_fd, VIDIOC_G_STD, &std) == -1) {
    log(pvrERROR, "error VIDIOC_G_STD on /dev/video%d (%s): %d:%s", \
       number, CARDNAME[cardname], errno, strerror(errno));
      }
  if (std & V4L2_STD_625_50) {
    CurrentLinesPerFrame = 625;
    }
  else {
    CurrentLinesPerFrame = 525;
    }
  /* the driver will automatically set the height to 576 or 480 on each change */  
  CurrentNorm = std;
  log(pvrINFO, "cPvrDevice::CurrentNorm=0x%08llx, CurrentLinesPerFrame=%d on /dev/video%d (%s)", \
      std,  CurrentLinesPerFrame, number, CARDNAME[cardname]);
}

void cPvrDevice::StopReadThread(void) {
  if (readThreadRunning) {
    log(pvrDEBUG2,"cPvrDevice::StopReadThread on /dev/video%d (%s): read thread exists, delete it", \
        number, CARDNAME[cardname]);
    cPvrReadThread * readThread_tmp = readThread;
    readThread = NULL;
    delete readThread_tmp;
  }
  else {
    log(pvrDEBUG2,"cPvrDevice::StopReadThread: no read thread running on /dev/video%d (%s)", \
        number, CARDNAME[cardname]);
    }
}

void cPvrDevice::ReInitAll(void) {
  log(pvrDEBUG1,"cPvrDevice::ReInitAll");
  int i;
  for (i = 0; i < kMaxPvrDevices; i++) {
    if (PvrDevices[i]) {
      PvrDevices[i]->ReInit();
      }
    }
}

int cPvrDevice::Count() {
  int count = 0;
  for (int i = 0; i < kMaxPvrDevices; i++) {
    if (PvrDevices[i])
      count++;
    }
  return count;
}

cPvrDevice * cPvrDevice::Get(int index) {
  int count = 0;
  for (int i = 0; i < kMaxPvrDevices; i++) {
    if (PvrDevices[i]) {
      if (count == index)
        return PvrDevices[i];
      count++;
    }
  }
  return NULL;
}

void cPvrDevice::ReInit(void) {
  log(pvrDEBUG1,"cPvrDevice::ReInit /dev/video%d = %s (%s)", number, CARDNAME[cardname], DRIVERNAME[driver]);
  SetTunerAudioMode(PvrSetup.TunerAudioMode);
  SetInput(CurrentInput);
  /* begin new v4l2 ivtv controls */
  if ((driver == cx18) || (driver == hdpvr))
    streamType = V4L2_MPEG_STREAM_TYPE_MPEG2_TS;
  else
    streamType = (PvrSetup.StreamType.value == 0) ? V4L2_MPEG_STREAM_TYPE_MPEG2_PS : V4L2_MPEG_STREAM_TYPE_MPEG2_DVD;
  SetControlValue(&PvrSetup.StreamType, streamType);
  SetControlValue(&PvrSetup.AspectRatio, PvrSetup.AspectRatio.value);
  SetControlValue(&PvrSetup.AudioBitrate, PvrSetup.AudioBitrate.value);
  SetControlValue(&PvrSetup.AudioSampling, PvrSetup.AudioSampling.value);
  if (driver == hdpvr) {
    SetControlValue(&PvrSetup.HDPVR_AudioEncoding, PvrSetup.HDPVR_AudioEncoding.value);
    SetAudioInput(PvrSetup.HDPVR_AudioInput);
    }
  SetControlValue(&PvrSetup.VideoBitrateTV, PvrSetup.VideoBitrateTV.value);
  SetControlValue(&PvrSetup.BitrateMode, PvrSetup.BitrateMode.value);
  SetControlValue(&PvrSetup.GopSize, PvrSetup.GopSize.queryctrl.default_value);
  SetControlValue(&PvrSetup.GopClosure, PvrSetup.GopClosure.queryctrl.default_value);
  SetControlValue(&PvrSetup.BFrames, PvrSetup.BFrames.queryctrl.default_value);
  SetControlValue(&PvrSetup.FilterSpatialMode, PvrSetup.FilterSpatialMode.value);
  SetControlValue(&PvrSetup.FilterSpatial, PvrSetup.FilterSpatial.value);
  SetControlValue(&PvrSetup.FilterLumaSpatialType, PvrSetup.FilterLumaSpatialType.value);
  SetControlValue(&PvrSetup.FilterChromaSpatialType, PvrSetup.FilterChromaSpatialType.value);
  SetControlValue(&PvrSetup.FilterTemporalMode, PvrSetup.FilterTemporalMode.value);
  SetControlValue(&PvrSetup.FilterTemporal, PvrSetup.FilterTemporal.value);
  SetControlValue(&PvrSetup.FilterMedianType, PvrSetup.FilterMedianType.value);
  SetControlValue(&PvrSetup.FilterLumaMedianBottom, PvrSetup.FilterLumaMedianBottom.value);
  SetControlValue(&PvrSetup.FilterLumaMedianTop, PvrSetup.FilterLumaMedianTop.value);
  SetControlValue(&PvrSetup.FilterChromaMedianBottom, PvrSetup.FilterChromaMedianBottom.value);
  SetControlValue(&PvrSetup.FilterChromaMedianTop, PvrSetup.FilterChromaMedianTop.value);
  SetControlValue(&PvrSetup.Brightness, PvrSetup.Brightness.value);
  SetControlValue(&PvrSetup.Contrast, PvrSetup.Contrast.value);
  SetControlValue(&PvrSetup.Saturation, PvrSetup.Saturation.value);
  SetControlValue(&PvrSetup.Hue, PvrSetup.Hue.value);
  SetControlValue(&PvrSetup.VideoBitrateTV, PvrSetup.VideoBitrateTV.value);
  if ((radio_fd >= 0) || (driver == pvrusb2 && CurrentInput == inputs[eRadio])) {
    SetControlValue(&PvrSetup.AudioVolumeFM, PvrSetup.AudioVolumeFM.value);
    SetControlValue(&PvrSetup.AudioMute, (int) (PvrSetup.AudioVolumeFM.value == 0));
    }
  else { //no radio
    SetAudioVolumeTV();
    SetControlValue(&PvrSetup.AudioMute, (int) (PvrSetup.AudioVolumeTVCommon.value == 0));
    }
}

bool cPvrDevice::Tune(int freq) {
  double fac = 16;
  int freqaux = freq;
  struct v4l2_frequency vf;
  if (frequency == freq) return true;
  if ((EncoderInput == eRadio) || (EncoderInput == eTelevision)) {
    memset(&vf, 0, sizeof(vf));
    struct v4l2_tuner tuner;
    memset(&tuner, 0, sizeof(tuner));
    if (IOCTL(v4l2_fd, VIDIOC_G_TUNER,&tuner) == 0) {
      fac = (tuner.capability & V4L2_TUNER_CAP_LOW) ? 16000 : 16;
      }
    vf.tuner = 0;
    vf.type = tuner.type;
    vf.frequency = (int) ((double) freqaux * fac / 1000.0);
    if (IOCTL(v4l2_fd, VIDIOC_S_FREQUENCY, &vf) == 0) {
      log(pvrDEBUG1, "cPvrDevice::Tune(): set Frequency on /dev/video%d (%s) to %.2f MHz (%d)", \
        number, CARDNAME[cardname], vf.frequency / fac, vf.frequency);
      }
    else {
      log(pvrERROR, "cPvrDevice::Tune(): error on /dev/video%d (%s) tuning to %.2f MHz (%d): %d:%s", \
          number, CARDNAME[cardname], vf.frequency / fac, vf.frequency, errno, strerror(errno));
      return false;
      }
    }
  frequency = freq;
  return true;
}

bool cPvrDevice::SetInput(int input) {
  if (input == CurrentInput && !FirstChannelSwitch) return true;
  log(pvrDEBUG1,"cPvrDevice::SetInput on /dev/video%d (%s) to %d", number, CARDNAME[cardname], input);
  if (IOCTL(v4l2_fd, VIDIOC_S_INPUT, &input) != 0) {
    log(pvrERROR, "VIDIOC_S_INPUT failed on /dev/video%d (%s) for input%d, %d:%s",
      number, CARDNAME[cardname], errno, strerror(errno));
    return false;
    }
#if 1 /* workaround for pvrusb2 driver bug: no audio after switching from radio to TV */
  if (input == 0 && CurrentInput == 3 && driver == pvrusb2) {
    usleep(200000); /* 200msec */
    log(pvrDEBUG2,"cPvrDevice::SetInput on /dev/video%d (%s) again to %d (workaround for driver bug)",
    number, CARDNAME[cardname], input);
    IOCTL(v4l2_fd, VIDIOC_S_INPUT, &input); //set input television again
    }
#endif
  CurrentInput=input;
  if (driver == pvrusb2) {
    usleep(100000); /* 100msec */
    if (CurrentInput == inputs[eRadio]) {
       SetControlValue(&PvrSetup.AudioVolumeFM,
         PvrSetup.AudioVolumeFM.value);
       }
    else { //television or extern
       SetAudioVolumeTV();
       }
    }
  return true;
}

bool cPvrDevice::SetAudioInput(int input) {
  struct v4l2_audio  a;
  a.index = input;
  log(pvrDEBUG1,"cPvrDevice::SetAudioInput on /dev/video%d (%s) to %d", number, CARDNAME[cardname], input);
  if (IOCTL(v4l2_fd, VIDIOC_S_AUDIO, &a) != 0) {
    log(pvrERROR, "VIDIOC_S_AUDIO failed on /dev/video%d (%s) for input%d, %d:%s",
      number, CARDNAME[cardname], errno, strerror(errno));
    return false;
    }
  return true;
}

bool cPvrDevice::SetVideoNorm(uint64_t norm) {
  if (norm == CurrentNorm){
    return true;
    }
  log(pvrDEBUG1, "SetVideoNorm(0x%08llx) for /dev/video%d (%s)", norm, number, CARDNAME[cardname]);
  if (hasDecoder) {
     log(pvrDEBUG1, "cPvrDevice::we need to stop the PVR350 decoder");
     struct video_command cmd;
     memset(&cmd, 0, sizeof(cmd));
     cmd.cmd = VIDEO_CMD_STOP;
     cmd.flags = VIDEO_CMD_STOP_TO_BLACK | VIDEO_CMD_STOP_IMMEDIATELY;
     if (IOCTL(v4l2_fd, VIDEO_COMMAND, &cmd) < 0) {
        log(pvrERROR, "pvrinput: VIDEO_CMD_STOP error on /dev/video%d=%d:%s", number, strerror(errno));
        }
     }
  if (IOCTL(v4l2_fd, VIDIOC_S_STD, &norm) !=0) {
    log(pvrERROR, "cPvrDevice::SetVideoNorm() on /dev/video%d (%s) failed, %d:%s", \
       number, CARDNAME[cardname], errno, strerror(errno));
     return false;
     }
  CurrentNorm=norm;
  return true;
}

bool cPvrDevice::SetVideoSize(int width, int height) {
  log(pvrDEBUG1,"cPvrDevice::SetVideoSize");
  struct v4l2_format vfmt;

  vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (IOCTL(v4l2_fd, VIDIOC_G_FMT, &vfmt) != 0) {
    log(pvrERROR, "cPvrDevice::SetVideoSize(): VIDIOC_G_FMT failed on /dev/video%d (%s), %d:%s", \
      number, CARDNAME[cardname], errno, strerror(errno));
    return false;
    }
  vfmt.fmt.pix.width  = width;
  vfmt.fmt.pix.height = height;
  if (IOCTL(v4l2_fd, VIDIOC_S_FMT, &vfmt) != 0) {
    log(pvrERROR, "cPvrDevice::SetVideoSize(): VIDIOC_S_FMT failed on /dev/video%d (%s), %d:%s", \
      number, CARDNAME[cardname], errno, strerror(errno));
    return false;
    }
  return true;
}

void cPvrDevice::SetTunerAudioMode(int tuneraudiomode) {
  if (!hasTuner)
    return;
  log(pvrDEBUG1, "cPvrDevice::SetTunerAudioMode(%s) on /dev/video%d (%s)", \
     tuneraudiomode == V4L2_TUNER_MODE_MONO?"mono":
     tuneraudiomode == V4L2_TUNER_MODE_STEREO?"stereo":
     tuneraudiomode == V4L2_TUNER_MODE_LANG1?"lang1":
     tuneraudiomode == V4L2_TUNER_MODE_LANG2?"lang2":"bilingual", \
     number, CARDNAME[cardname]);
  struct v4l2_tuner vt;
  memset(&vt, 0, sizeof(vt));
  vt.audmode = tuneraudiomode;
  if (IOCTL(v4l2_fd, VIDIOC_S_TUNER, &vt)) 
    log(pvrERROR, "cPvrDevice::SetTunerAudioMode(%d): error %d:%s on /dev/video%d (%s)", \
          tuneraudiomode, errno, strerror(errno), number, CARDNAME[cardname]); 
}

void cPvrDevice::SetAudioVolumeTV() {
  log(pvrDEBUG2, "AudioVolumeTVException.value=%d; AudioVolumeTVCommon.value=%d; number=%d, AudioVolumeTVExceptionCard=%d", \
                  PvrSetup.AudioVolumeTVException.value, PvrSetup.AudioVolumeTVCommon.value, number, PvrSetup.AudioVolumeTVExceptionCard);
  if ((PvrSetup.AudioVolumeTVExceptionCard >= 0 && PvrSetup.AudioVolumeTVExceptionCard <=7) \
        && (number == PvrSetup.AudioVolumeTVExceptionCard)) { //special value selected for a /dev/videoX number
            log(pvrDEBUG1, "cPvrDevice::SetAudioVolumeTVException %d for /dev/video%d (%s)", \
                  PvrSetup.AudioVolumeTVException.value, number, CARDNAME[cardname]);
            SetControlValue(&PvrSetup.AudioVolumeTVException, PvrSetup.AudioVolumeTVException.value);
            }
  if (PvrSetup.AudioVolumeTVExceptionCard >= 9 && PvrSetup.AudioVolumeTVExceptionCard <=18) { //special value for card(s) with certain name
    if ((cardname == PVR150   && PvrSetup.AudioVolumeTVExceptionCard ==9)  ||
        (cardname == PVR250   && PvrSetup.AudioVolumeTVExceptionCard ==10) ||
        (cardname == PVR350   && PvrSetup.AudioVolumeTVExceptionCard ==11) ||
        (cardname == PVR500_1 && PvrSetup.AudioVolumeTVExceptionCard ==12) ||
        (cardname == PVR500_2 && PvrSetup.AudioVolumeTVExceptionCard ==13) ||
        (cardname == HVR1300  && PvrSetup.AudioVolumeTVExceptionCard ==14) ||
        (cardname == HVR1600  && PvrSetup.AudioVolumeTVExceptionCard ==15) ||
        (cardname == HVR1900  && PvrSetup.AudioVolumeTVExceptionCard ==16) ||
        (cardname == HVR1950  && PvrSetup.AudioVolumeTVExceptionCard ==17) ||
        (cardname == PVRUSB2  && PvrSetup.AudioVolumeTVExceptionCard ==18) ) {
            log(pvrDEBUG1, "cPvrDevice::SetAudioVolumeTVException %d for /dev/video%d (%s)", \
                  PvrSetup.AudioVolumeTVException.value, number, CARDNAME[cardname]);
            SetControlValue(&PvrSetup.AudioVolumeTVException, PvrSetup.AudioVolumeTVException.value);
            }
    }
  else { //no special value (PvrSetup.AudioVolumeTVExceptionCard ==8)
    log(pvrDEBUG1, "cPvrDevice::SetAudioVolumeTVCommon %d for /dev/video%d (%s)", \
           PvrSetup.AudioVolumeTVCommon.value, number, CARDNAME[cardname]);
     SetControlValue(&PvrSetup.AudioVolumeTVCommon, PvrSetup.AudioVolumeTVCommon.value);
     }
}

void cPvrDevice::SetEncoderState(eEncState state) {
  log(pvrDEBUG1, "cPvrDevice::SetEncoderState (%s) for /dev/video%d (%s)", \
      state==eStop?"Stop":"Start", number, CARDNAME[cardname]);
  if (driver == ivtv || driver == cx18 || driver == hdpvr) {
    struct v4l2_encoder_cmd encoderCommand;
    memset(&encoderCommand, 0, sizeof(encoderCommand));
    switch(state) {
      case eStop  :  encoderCommand.cmd = V4L2_ENC_CMD_STOP;   break;
      case eStart :  encoderCommand.cmd = V4L2_ENC_CMD_START;  break;
      }
    if (IOCTL(v4l2_fd, VIDIOC_ENCODER_CMD, &encoderCommand)) 
      log(pvrERROR, "cPvrDevice::SetEncoderState(%s): error %d:%s on /dev/video%d (%s)", \
          state==eStop?"Stop":"Start", errno, strerror(errno), number, CARDNAME[cardname]); 
    }
  if (driver == pvrusb2 && state == eStop) {
    int retry_count = 5;
    retry:
    close(v4l2_fd);
    v4l2_fd = -1;
    log(pvrDEBUG2, "cPvrDevice::SetEncoderState (eStop): /dev/video%d (%s) is closed", \
        number, CARDNAME[cardname]);
    pvrusb2_ready = false;
    char * devName;
    if (asprintf(&devName, "/dev/video%d", number) != -1) {
      v4l2_fd = open(devName, O_RDWR);  //reopen for tuning
      }
    if (v4l2_fd < 0) {
      log(pvrERROR, "cPvrDevice::SetEncoderState(eStop): error reopening %s (%s): %d:%s", \
          CARDNAME[cardname], devName, errno, strerror(errno));
      free(devName);
      retry_count--;
      if (retry_count > 0)
        goto retry;
      }
    else {
      log(pvrDEBUG2, "cPvrDevice::SetEncoderState (eStop): %s (%s) successfully re-opened", \
          devName, CARDNAME[cardname]);
      pvrusb2_ready = true;
      free(devName);
      }
   }
}

bool cPvrDevice::SetVBImode(int vbiLinesPerFrame, int vbistatus) {
  if (v4l2_fd >= 0 && SupportsSlicedVBI ) {
    log(pvrDEBUG1, "SetVBImode(%d, %d) on /dev/video%d (%s)", \
        vbiLinesPerFrame, vbistatus, number, CARDNAME[cardname]);
    struct v4l2_format vbifmt;
    struct v4l2_ext_controls ctrls;
    struct v4l2_ext_control  ctrl;
    memset(&vbifmt, 0, sizeof(vbifmt));
    memset(&ctrls,  0, sizeof(ctrls));
    memset(&ctrl,   0, sizeof(ctrl));
    ctrl.id    = V4L2_CID_MPEG_STREAM_VBI_FMT;
    ctrl.value = vbistatus; 
    /*  0 = V4L2_MPEG_STREAM_VBI_FMT_NONE,  No VBI in the MPEG stream
        1 = V4L2_MPEG_STREAM_VBI_FMT_IVTV,  VBI in private packets, IVTV format */
    ctrls.ctrl_class=V4L2_CTRL_CLASS_MPEG;
    ctrls.controls=&ctrl;
    ctrls.count=1;
    if (IOCTL(v4l2_fd, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
      log(pvrERROR, "cPvrDevice::SetVBImode(): error setting vbi mode (ctrls) on /dev/video%d (%s), %d:%s",
        number, CARDNAME[cardname], errno, strerror(errno));
      return false; 
      }
    if (ctrl.value == V4L2_MPEG_STREAM_VBI_FMT_IVTV && vbiLinesPerFrame == 625) {
        vbifmt.fmt.sliced.service_set = V4L2_SLICED_TELETEXT_B;
        vbifmt.type = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;
        vbifmt.fmt.sliced.reserved[0] = 0;
        vbifmt.fmt.sliced.reserved[1] = 0;

        if (IOCTL(v4l2_fd, VIDIOC_S_FMT, &vbifmt) < 0) {
          log(pvrERROR, "cPvrDevice::SetVBImode():error setting vbi mode (fmt) on /dev/video%d (%s), %d:%s",
              number, CARDNAME[cardname], errno, strerror(errno));
          return false;
          }
        }
    }
  return true;
}

bool cPvrDevice::ParseChannel(const cChannel *Channel, int *input, uint64_t *norm, int *LinesPerFrame,
                              int *card, eVideoInputs *EncoderInput, int *apid, int *vpid, int *tpid) const {
  *card  = 999;
  *norm  = CurrentNorm; //make sure we have a value if channels.conf has no optArg for norm
  *LinesPerFrame = CurrentLinesPerFrame; //see above
  *input = -1;
  if (Channel->IsCable() && Channel->Ca() == 0xA1) {
    Skins.Message(mtError, tr("pvrinput no longer supports old channel syntax!"), 2);
    log(pvrERROR, "cPvrDevice::pvrinput no longer supports old channel syntax!");
    }
#ifdef PVR_SOURCEPARAMS
  if (cPvrSourceParam::IsPvr(Channel->Source())) {
    const char * str = Channel->Parameters();
    const char * PluginId = "PVRINPUT";
    char * Input    = NULL;
    char * optArg1  = NULL;
    char * optArg2  = NULL;
    int NumArgs = 1 + sscanf(str, "%a[^|]|%a[^|]|%a[^:\n]",
                        &Input, &optArg1, &optArg2);
#else
  if (Channel->IsPlug()) {
    const char * str = Channel->PluginParam();
    char * PluginId = NULL;
    char * Input    = NULL;
    char * optArg1  = NULL;
    char * optArg2  = NULL;
    int NumArgs = sscanf(str, "%a[^|]|%a[^|]|%a[^|]|%a[^:\n]",
                    &PluginId, &Input, &optArg1, &optArg2);
#endif

    if (NumArgs >= 2) {
      if (strcasecmp(PluginId, "PVRINPUT")) return false;
      log(pvrDEBUG1, "cPvrDevice::ParseChannel: using iptv channel syntax.");
      *vpid  = Channel->Vpid();
      *apid  = Channel->Apid(0);
      *tpid  = Channel->Tpid();
      if      (!strcasecmp (Input, "RADIO"))           *EncoderInput=eRadio;
      else if (!strcasecmp (Input, "TV"))              *EncoderInput=eTelevision;
      else if (!strncasecmp(Input, "COMPOSITE",9))     *EncoderInput=eExternalInput;
      else if (!strncasecmp(Input, "SVIDEO",6))        *EncoderInput=eExternalInput;
      else if (!strncasecmp(Input, "COMPONENT",9))     *EncoderInput=eExternalInput;
      else return false;
      if      (!strcasecmp (Input, "TV"))              *input = inputs[eTelevision];
      else if (!strcasecmp (Input, "RADIO"))           *input = inputs[eRadio];
      else if (!strcasecmp (Input, "COMPOSITE0"))      *input = inputs[eComposite0];
      else if (!strcasecmp (Input, "COMPOSITE1"))      *input = inputs[eComposite1];
      else if (!strcasecmp (Input, "COMPOSITE2"))      *input = inputs[eComposite2];
      else if (!strcasecmp (Input, "COMPOSITE3"))      *input = inputs[eComposite3];
      else if (!strcasecmp (Input, "COMPOSITE4"))      *input = inputs[eComposite4];
      else if (!strcasecmp (Input, "COMPOSITE"))       *input = inputs[eComposite0];
      else if (!strcasecmp (Input, "SVIDEO0"))         *input = inputs[eSVideo0];
      else if (!strcasecmp (Input, "SVIDEO1"))         *input = inputs[eSVideo1];
      else if (!strcasecmp (Input, "SVIDEO2"))         *input = inputs[eSVideo2];
      else if (!strcasecmp (Input, "SVIDEO3"))         *input = inputs[eSVideo3];
      else if (!strcasecmp (Input, "SVIDEO"))          *input = inputs[eSVideo0];
      else if (!strcasecmp (Input, "COMPONENT"))       *input = inputs[eComponent];
      else return false; //unknown
      if (NumArgs > 2) { /* only if optional arg norm or card given */
        if      (!strcasecmp  (optArg1, "PAL"))        { *norm = V4L2_STD_PAL;       *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "NTSC" ))      { *norm = V4L2_STD_NTSC;      *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg1, "SECAM"))      { *norm = V4L2_STD_SECAM;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "NTSC_M"))     { *norm = V4L2_STD_NTSC_M;    *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg1, "NTSC_M_JP"))  { *norm = V4L2_STD_NTSC_M_JP; *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg1, "NTSC_443"))   { *norm = V4L2_STD_NTSC_443;  *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg1, "NTSC_M_KR"))  { *norm = V4L2_STD_NTSC_M_KR; *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg1, "PAL_M"))      { *norm = V4L2_STD_PAL_M;     *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg1, "PAL_N"))      { *norm = V4L2_STD_PAL_N;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_NC"))     { *norm = V4L2_STD_PAL_Nc;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_B"))      { *norm = V4L2_STD_PAL_B;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_B1"))     { *norm = V4L2_STD_PAL_B1;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_G"))      { *norm = V4L2_STD_PAL_G;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_BG"))     { *norm = V4L2_STD_PAL_BG;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_D"))      { *norm = V4L2_STD_PAL_D;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_D1"))     { *norm = V4L2_STD_PAL_D1;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_K"))      { *norm = V4L2_STD_PAL_K;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_DK"))     { *norm = V4L2_STD_PAL_DK;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_H"))      { *norm = V4L2_STD_PAL_H;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_I"))      { *norm = V4L2_STD_PAL_I;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "PAL_60"))     { *norm = V4L2_STD_PAL_60;    *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg1, "SECAM_B"))    { *norm = V4L2_STD_SECAM_B;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "SECAM_D"))    { *norm = V4L2_STD_SECAM_D;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "SECAM_G"))    { *norm = V4L2_STD_SECAM_G;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "SECAM_H"))    { *norm = V4L2_STD_SECAM_H;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "SECAM_K"))    { *norm = V4L2_STD_SECAM_K;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "SECAM_K1"))   { *norm = V4L2_STD_SECAM_K1;  *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "SECAM_DK"))   { *norm = V4L2_STD_SECAM_DK;  *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "SECAM_L"))    { *norm = V4L2_STD_SECAM_L;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg1, "SECAM_LC"))   { *norm = V4L2_STD_SECAM_LC;  *LinesPerFrame = 625; }
        if      (!strcasecmp  (optArg1, "CARD0"))      *card = 0;
        else if (!strcasecmp  (optArg1, "CARD1"))      *card = 1;
        else if (!strcasecmp  (optArg1, "CARD2"))      *card = 2;
        else if (!strcasecmp  (optArg1, "CARD3"))      *card = 3;
        else if (!strcasecmp  (optArg1, "CARD4"))      *card = 4;
        else if (!strcasecmp  (optArg1, "CARD5"))      *card = 5;
        else if (!strcasecmp  (optArg1, "CARD6"))      *card = 6;
        else if (!strcasecmp  (optArg1, "CARD7"))      *card = 7;
        }
      if (NumArgs > 3) { /* only if optional arg norm or card given */
        if      (!strcasecmp  (optArg2, "PAL"))        { *norm = V4L2_STD_PAL;       *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "NTSC" ))      { *norm = V4L2_STD_NTSC;      *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg2, "SECAM"))      { *norm = V4L2_STD_SECAM;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "NTSC_M"))     { *norm = V4L2_STD_NTSC_M;    *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg2, "NTSC_M_JP"))  { *norm = V4L2_STD_NTSC_M_JP; *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg2, "NTSC_443"))   { *norm = V4L2_STD_NTSC_443;  *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg2, "NTSC_M_KR"))  { *norm = V4L2_STD_NTSC_M_KR; *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg2, "PAL_M"))      { *norm = V4L2_STD_PAL_M;     *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg2, "PAL_N"))      { *norm = V4L2_STD_PAL_N;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_NC"))     { *norm = V4L2_STD_PAL_Nc;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_B"))      { *norm = V4L2_STD_PAL_B;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_B1"))     { *norm = V4L2_STD_PAL_B1;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_G"))      { *norm = V4L2_STD_PAL_G;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_BG"))     { *norm = V4L2_STD_PAL_BG;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_D"))      { *norm = V4L2_STD_PAL_D;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_D1"))     { *norm = V4L2_STD_PAL_D1;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_K"))      { *norm = V4L2_STD_PAL_K;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_DK"))     { *norm = V4L2_STD_PAL_DK;    *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_H"))      { *norm = V4L2_STD_PAL_H;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_I"))      { *norm = V4L2_STD_PAL_I;     *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "PAL_60"))     { *norm = V4L2_STD_PAL_60;    *LinesPerFrame = 525; }
        else if (!strcasecmp  (optArg2, "SECAM_B"))    { *norm = V4L2_STD_SECAM_B;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "SECAM_D"))    { *norm = V4L2_STD_SECAM_D;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "SECAM_G"))    { *norm = V4L2_STD_SECAM_G;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "SECAM_H"))    { *norm = V4L2_STD_SECAM_H;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "SECAM_K"))    { *norm = V4L2_STD_SECAM_K;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "SECAM_K1"))   { *norm = V4L2_STD_SECAM_K1;  *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "SECAM_DK"))   { *norm = V4L2_STD_SECAM_DK;  *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "SECAM_L"))    { *norm = V4L2_STD_SECAM_L;   *LinesPerFrame = 625; }
        else if (!strcasecmp  (optArg2, "SECAM_LC"))   { *norm = V4L2_STD_SECAM_LC;  *LinesPerFrame = 625; }
        if      (!strcasecmp  (optArg2, "CARD0"))      *card = 0;
        else if (!strcasecmp  (optArg2, "CARD1"))      *card = 1;
        else if (!strcasecmp  (optArg2, "CARD2"))      *card = 2;
        else if (!strcasecmp  (optArg2, "CARD3"))      *card = 3;
        else if (!strcasecmp  (optArg2, "CARD4"))      *card = 4;
        else if (!strcasecmp  (optArg2, "CARD5"))      *card = 5;
        else if (!strcasecmp  (optArg2, "CARD6"))      *card = 6;
        else if (!strcasecmp  (optArg2, "CARD7"))      *card = 7;
        }
      log(pvrDEBUG2, "ParseChannel %s input %d, norm=0x%08llx, card %d",
              (*EncoderInput==eRadio)?"Radio":(*EncoderInput==eTelevision)?"TV":"Ext",
              *input,*norm,*card);
      return true;
      }  
    }
  return false;
}

bool cPvrDevice::SetChannelDevice(const cChannel * Channel, bool LiveView) {
  log(pvrDEBUG1, "cPvrDevice::SetChannelDevice %d (%s) %3.2fMHz (/dev/video%d = %s)", \
    Channel->Number(), Channel->Name(), (double) Channel->Frequency()/1000,  number, CARDNAME[cardname]);
  int input, LinesPerFrame, card;
  uint64_t norm;
  if (! ParseChannel(Channel,&input,&norm,&LinesPerFrame,&card,&EncoderInput,&apid,&vpid,&tpid))
     return false;

  if ((Channel->Number() == currentChannel.Number()) && (Channel->Frequency() == frequency) && (input == CurrentInput) && (norm == CurrentNorm))
    return true;
  newFrequency = Channel->Frequency();
  newInput = input;
  newNorm = norm;
  newLinesPerFrame = LinesPerFrame;
  newEncoderInput = EncoderInput;
  ChannelSettingsDone = false;
  currentChannel = *Channel;
  return true;
}

bool cPvrDevice::SetPid(cPidHandle * Handle, int Type, bool On) {
  log(pvrDEBUG2, "cPvrDevice::SetPid %d = %s", Handle->pid, On?"On":"Off");
  return true;
}

int  cPvrDevice::OpenFilter(u_short Pid, u_char Tid, u_char Mask)
{
  int handle = sectionHandler.AddFilter(Pid, Tid, Mask);
  log(pvrDEBUG2, "cPvrDevice::OpenFilter: /dev/video%d (%s) pid = %d, tid = %d, mask = %d, handle = %d",
      number, CARDNAME[cardname], Pid, Tid, Mask, handle);
  return handle;
}

#if VDRVERSNUM >= 10600
void cPvrDevice::CloseFilter(int Handle)
{
  log(pvrDEBUG2, "cPvrDevice::CloseFilter: /dev/video%d (%s) handle = %d",
      number, CARDNAME[cardname], Handle);
  sectionHandler.RemoveFilter(Handle);
}
#endif

bool cPvrDevice::OpenDvr(void) {
  log(pvrDEBUG1, "entering cPvrDevice::OpenDvr: Dvr of /dev/video%d (%s) is %s",
      number, CARDNAME[cardname], (dvrOpen)?"open":"closed");
  delivered = false;
  CloseDvr();
  while (dvrOpen) { //wait until CloseDvr has finnished
    usleep(40000);
    log(pvrDEBUG1, "OpenDvr: wait for CloseDvr on /dev/video%d (%s) to finnish", \
        number, CARDNAME[cardname]);
    }
  tsBuffer->Clear();
  ResetBuffering();
  if (!dvrOpen) {
     if (driver == ivtv || driver == cx18) {
       if (PvrSetup.repeat_ReInitAll_after_next_encoderstop) {
          ReInitAll(); //some settings require an encoder stop, so we repeat them now
          PvrSetup.repeat_ReInitAll_after_next_encoderstop = false;
          }
       }
     if (!ChannelSettingsDone) {
       if (driver == pvrusb2) {
          while (!pvrusb2_ready) { //re-opening pvrusb2 in SetEncoderState might be in progress
            usleep(40000);
            log(pvrDEBUG1, "OpenDvr: wait until /dev/video%d (%s) is ready", \
              number, CARDNAME[cardname]);
            }
          } 
       switch (newEncoderInput) {
         case eComposite0 ... eComposite4:   //no break here, continuing at next case item
         case eSVideo0 ... eSVideo3:         //no break here, continuing at next case item
         case eComponent:                    //no break here, continuing at next case item
         case eExternalInput:
               {
               log(pvrDEBUG2,"channel is external input.");
               if (radio_fd >= 0) {
                 close(radio_fd);
                 radio_fd = -1;
                 usleep(100000); /* 100msec */
                 SetAudioVolumeTV();
                 SetControlValue(&PvrSetup.VideoBitrateTV, \
                           PvrSetup.VideoBitrateTV.value);
                 }

               if (PvrSetup.UseExternChannelSwitchScript) {
                 cString cmd = cString::sprintf("%s %d %d %d", *externChannelSwitchScript, currentChannel.Sid(), currentChannel.Number(), number);
                 log(pvrDEBUG1, "OpenDvr: calling %s", *cmd);
                 if (system(*cmd) < 0) {
                   log(pvrERROR, "OpenDvr: executing %s failed", *cmd);
                   }
                 log(pvrDEBUG1, "OpenDvr: returned from %s", *cmd);
                 }

               if (! SetInput(newInput))    return false;
               if (! SetVideoNorm(newNorm)) return false;
               frequency=newFrequency; // since we don't tune: set it here          
               break;
               }
         case eRadio   :
               {
               log(pvrDEBUG2,"channel is FM radio.");
               switch (driver) {
                 case ivtv:
                 case cx18:
                   if (radio_dev < 0) return false; //no hardware support.
                   if (radio_fd < 0) {
                     char * devName;
                     if (asprintf(&devName, "/dev/radio%d", radio_dev) != -1) {
                       radio_fd = open(devName, O_RDONLY);
                       }
                     if (radio_fd < 0) {
                       log(pvrERROR, "Error opening FM radio device %s: %s",\
                         devName, strerror(errno));
                       return false;
                       }
                     free(devName);
                     usleep(100000); /* 100msec */
                     SetControlValue(&PvrSetup.AudioVolumeFM, \
                       PvrSetup.AudioVolumeFM.value);
                     }
                   break;
                 case pvrusb2:
                   if (! SetInput(inputs[eRadio]))   return false;
                 case cx88_blackbird:
                 case hdpvr:
                   break;
                 case undef: log(pvrERROR, "driver is unknown!!"); return false;
                 }
               if (! Tune(newFrequency)) return false;
               break;
               }
         case eTelevision :
               {
               log(pvrDEBUG2,"channel is television.");
               if ((driver == ivtv) && (radio_fd >= 0)) {
                 close(radio_fd);
                 radio_fd = -1;
                 usleep(100000); /* 100msec */
                 SetAudioVolumeTV();
                 SetControlValue(&PvrSetup.VideoBitrateTV, \
                           PvrSetup.VideoBitrateTV.value);
                 }
               if (! SetInput(inputs[eTelevision]))   return false;
               if (! SetVideoNorm(newNorm)) return false;
               if (! SetVBImode(newLinesPerFrame, PvrSetup.SliceVBI)) return false;
               if (! Tune(newFrequency)) return false;
               }
         } //end: switch (newEncoderInput)
       ChannelSettingsDone = true;
       FirstChannelSwitch = false;
       } //end: if ((!ChannelSettingsDone)
     SetEncoderState(eStart);
     if (!readThreadRunning) {
       log(pvrDEBUG2, "cPvrDevice::OpenDvr: create new readThread on /dev/video%d (%s)", \
           number, CARDNAME[cardname]);
       readThread = new cPvrReadThread(tsBuffer, this);
       }
     } //end: if (!dvrOpen)
  dvrOpen = true;
  return true;
}

void cPvrDevice::CloseDvr(void) {
  if (isClosing) return;
  isClosing = true;
  log(pvrDEBUG2, "entering cPvrDevice::CloseDvr: Dvr of /dev/video%d (%s) is %s", \
      number, CARDNAME[cardname], (dvrOpen)?"open":"closed");
  if (dvrOpen) {
     StopReadThread();
     SetEncoderState(eStop);
     SetVBImode(CurrentLinesPerFrame, 0);
     }
  dvrOpen = false;
  isClosing = false;
}

void cPvrDevice::ResetBuffering() {
  tsBufferPrefill = (MEGABYTE(PvrSetup.TsBufferSizeMB) * PvrSetup.TsBufferPrefillRatio) / 100;
  tsBufferPrefill -= (tsBufferPrefill % TS_SIZE);
  log(pvrDEBUG2, "cPvrDevice::ResetBuffering(): tsBuffer prefill = %d for /dev/video%d (%s)",
      tsBufferPrefill, number, CARDNAME[cardname]);
}

bool cPvrDevice::IsBuffering() {
  int avail = tsBuffer->Available();
  if (tsBufferPrefill && (avail < tsBufferPrefill)) {
    log(pvrDEBUG2, "cPvrDevice::IsBuffering(): available = %d, prefill = %d for /dev/video%d (%s)",
        avail, tsBufferPrefill, number, CARDNAME[cardname]);
    return true;
    }
  tsBufferPrefill = 0;
  return false;
}

bool cPvrDevice::GetTSPacket(uchar *&Data) {
  int Count = 0;
  if (! tsBuffer ) {
    log(pvrERROR, "cPvrDevice::GetTSPacket(): no tsBuffer for /dev/video%d (%s)", \
        number, CARDNAME[cardname]);
    return false;
    }
  if (tsBuffer && readThreadRunning) {
    if (!IsBuffering()) {
      if (delivered) {
        tsBuffer->Del(TS_SIZE);
        delivered = false;
        //log(pvrDEBUG2, "cPvrDevice::GetTSPacket(): packet delivered");
        }
       uchar *p = tsBuffer->Get(Count);
       if (p && Count >= TS_SIZE) {
        if (*p != TS_SYNC_BYTE) {
          for (int i = 1; i < Count; i++) {
            if (p[i] == TS_SYNC_BYTE) {
              Count = i;
              break;
              } //end: if
            } //end: for
          tsBuffer->Del(Count);
          log(pvrINFO, "ERROR: cPvrDevice::GetTSPacket(): skipped %d bytes to sync on TS packet\n", Count);
          return false;
          }
        sectionHandler.ProcessTSPacket(p);
        delivered = true;
        Data = p;
        return true;
        }
      }
    cCondWait::SleepMs(10); // reduce cpu load while buffering
    Data = NULL;
    return true;
    }
  return false;
}

bool cPvrDevice::ProvidesSource(int Source) const {
#ifdef PVR_SOURCEPARAMS
  if (cPvrSourceParam::IsPvr(Source)) {
#else
  if (cSource::IsPlug(Source)) {
#endif
    log(pvrDEBUG1, "cPvrDevice::ProvidesSource Source=%s -> true",
        (const char *) cSource::ToString(Source));
    return true;
    }
  return false;
}

bool cPvrDevice::ProvidesTransponder(const cChannel * Channel) const {
  return ProvidesSource(Channel->Source());
}

int cPvrDevice::NumProvidedSystems(void) const
{
  return 1;
}

bool cPvrDevice::ProvidesChannel(const cChannel * Channel, int Priority, \
                                 bool * NeedsDetachReceivers) const {
  bool result = false;
  bool hasPriority = Priority < 0 || Priority > this->Priority();
  bool needsDetachReceivers = true;
  int input, LinesPerFrame, dev;
  uint64_t norm;
  eVideoInputs encoderInput;          //only dummy
  int audioPid,videoPid,teletextPid;  //only dummy
  if (! ParseChannel(Channel, &input, &norm, &LinesPerFrame, &dev, &encoderInput, &audioPid, &videoPid, &teletextPid))
    return false; // non-pvrinput channel or wrong syntax used, silently ignore this one
  if ((dev < 999) && (number != dev)) {
    log(pvrDEBUG1, "cPvrDevice::ProvidesChannel %s -> false (this is /dev/video%d, but CARD parameter says /dev/video%d only)",
        Channel->Name(), number, dev); 
    return false;
    }
  if ((PvrSetup.UseOnlyCard >= 0 && PvrSetup.UseOnlyCard <=7) && (number != PvrSetup.UseOnlyCard)) { //select by number
    log(pvrDEBUG1, "cPvrDevice::ProvidesChannel %s -> false (this is /dev/video%d, but /dev/video%d only was selected in setup)",
        Channel->Name(), number, PvrSetup.UseOnlyCard);
    return false;
    }
  if (PvrSetup.UseOnlyCard >= 9 && PvrSetup.UseOnlyCard <=19) { //select by Name
    if ((cardname == PVR150   && PvrSetup.UseOnlyCard !=9)  ||
        (cardname == PVR250   && PvrSetup.UseOnlyCard !=10) ||
        (cardname == PVR350   && PvrSetup.UseOnlyCard !=11) ||
        (cardname == PVR500_1 && PvrSetup.UseOnlyCard !=12) ||
        (cardname == PVR500_2 && PvrSetup.UseOnlyCard !=13) ||        
        (cardname == HVR1300  && PvrSetup.UseOnlyCard !=14) ||
        (cardname == HVR1600  && PvrSetup.UseOnlyCard !=15) ||
        (cardname == HVR1900  && PvrSetup.UseOnlyCard !=16) ||
        (cardname == HVR1950  && PvrSetup.UseOnlyCard !=17) ||
        (cardname == PVRUSB2  && PvrSetup.UseOnlyCard !=18) ||
        (cardname == HDPVR    && PvrSetup.UseOnlyCard !=19) ) {
           log(pvrDEBUG1, "cPvrDevice::ProvidesChannel %s -> false (this is %s, but %s only was selected in setup)",
               Channel->Name(),CARDNAME[cardname],
               PvrSetup.UseOnlyCard==9?"PVR150": \
               PvrSetup.UseOnlyCard==10?"PVR250": \
               PvrSetup.UseOnlyCard==11?"PVR350": \
               PvrSetup.UseOnlyCard==12?"PVR500#1": \
               PvrSetup.UseOnlyCard==13?"PVR500#2": \
               PvrSetup.UseOnlyCard==14?"HVR1300": \
               PvrSetup.UseOnlyCard==15?"HVR1600": \
               PvrSetup.UseOnlyCard==16?"HVR1900": \
               PvrSetup.UseOnlyCard==17?"HVR1950": \
               PvrSetup.UseOnlyCard==18?"PVRUSB2": \
               PvrSetup.UseOnlyCard==19?"HDPVR":"UNDEF");
           return false;
           }
    }
  if (Channel->Frequency() >= 87500 && Channel->Frequency() <= 108000) {
    if (radio_dev < 0) {
      log(pvrDEBUG1, "cPvrDevice::ProvidesChannel: /dev/video%d (%s) has no radio", number, CARDNAME[cardname]);
      return false;
      }
    }
  if ((input < 0) || (input >= numInputs)) {
    log(pvrERROR, "cPvrDevice::ProvidesChannel: input %d unknown on /dev/video%d (%s)", \
        input, number, CARDNAME[cardname]);
    return false;
    }
  if ((Priority >= 0) && Receiving(true)) {
    if ((Channel->Number() == currentChannel.Number()) && (frequency == Channel->Frequency()) && (CurrentInput == input) && (CurrentNorm == norm)) {
      needsDetachReceivers = false;
      result = true;
      log(pvrDEBUG1, "cPvrDevice::ProvidesChannel: %s already set -> true.", Channel->Name());
      }
    else
      result = hasPriority;
    }
  else
    result = hasPriority;
  if (NeedsDetachReceivers)
    *NeedsDetachReceivers = needsDetachReceivers;
  log(pvrDEBUG1, "cPvrDevice::ProvidesChannel: /dev/video%d (%s): Channel %d (%s) %3.2fMHz, -> %s", \
    number, CARDNAME[cardname], Channel->Number(), Channel->Name(), (double) Channel->Frequency()/1000, \
    result?"true":"false");
  log(pvrDEBUG2, "cPvrDevice::ProvidesChannel: /dev/video%d: Receiving()=%s, needsDetachReceivers=%s, Priority=%d, hasPriority=%s", \
    number, Receiving(true)?"true":"false", needsDetachReceivers?"true":"false", Priority, hasPriority?"true":"false");
  return result;
}

/*
if a card/driver has unsupported controls, it returns false for this
*/
bool cPvrDevice::ControlIdIsValid(__u32 ctrlid) {
  if (driver == hdpvr) {
    if (/*(ctrlid != V4L2_CID_BRIGHTNESS) // these controls seem to use other values for the HD PVR
      && (ctrlid != V4L2_CID_CONTRAST)    // than the other PVRs (ioctl returns "invalid argument")
      && (ctrlid != V4L2_CID_SATURATION)  // so ignore them for now, since the defaults seem to be ok.
      && (ctrlid != V4L2_CID_HUE)
      &&*/ (ctrlid != V4L2_CID_SHARPNESS)
      && (ctrlid != V4L2_CID_MPEG_AUDIO_ENCODING)
      && (ctrlid != V4L2_CID_MPEG_VIDEO_ENCODING)
      && (ctrlid != V4L2_CID_MPEG_VIDEO_BITRATE_MODE)
      && (ctrlid != V4L2_CID_MPEG_VIDEO_BITRATE)
      && (ctrlid != V4L2_CID_MPEG_VIDEO_BITRATE_PEAK))
      return false;
    }
  return true;
}

/* 
Sets a V4L2 control.
If the value Val is out of range, the ivtv default value will be
used. The value set to V4L2 will be returned, either Val or the
default.
Has to be used *after* calling cPvrDevice::QueryAllControls()
20070506
If possible, use THIS function for setting VIDIOC_S_EXT_CTRLS.
-wirbel-
*/

int cPvrDevice::SetControlValue(struct valSet * vs, __s32 Val) {
  if (SetControlValue(vs->ctrl_class, vs->queryctrl.id, Val, vs->queryctrl) == 0) {
   vs->value = Val;
   return 0;
   }
  else
    return -1;
}

/*
functionality as above, but no valSet needed.
*/

int cPvrDevice::SetControlValue(__u32 control, __s32 Val) {

__u32 ctrl_class=V4L2_CTRL_CLASS_MPEG;
struct v4l2_queryctrl * query; 

switch (control) {
  // picture properties
  case V4L2_CID_BRIGHTNESS:    ctrl_class=V4L2_CTRL_CLASS_USER;
                               query=&PvrSetup.Brightness.queryctrl;
                               break;
  case V4L2_CID_CONTRAST:      ctrl_class=V4L2_CTRL_CLASS_USER;
                               query=&PvrSetup.Contrast.queryctrl;
                               break;
  case V4L2_CID_SATURATION:    ctrl_class=V4L2_CTRL_CLASS_USER;
                               query=&PvrSetup.Saturation.queryctrl;
                               break;
  case V4L2_CID_HUE:           ctrl_class=V4L2_CTRL_CLASS_USER;
                               query=&PvrSetup.Hue.queryctrl;
                               break;
  // Audio
  case V4L2_CID_AUDIO_VOLUME:  ctrl_class=V4L2_CTRL_CLASS_USER;
                               query=&PvrSetup.AudioVolumeTVCommon.queryctrl;
                               break;
  case V4L2_CID_AUDIO_MUTE:    ctrl_class=V4L2_CTRL_CLASS_USER;
                               query=&PvrSetup.AudioMute.queryctrl;
                               break;
  case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
                               query=&PvrSetup.AudioBitrate.queryctrl;
                               break;
  case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
                               query=&PvrSetup.AudioSampling.queryctrl;
                               break;
  case V4L2_CID_MPEG_AUDIO_ENCODING:
                               if (driver == hdpvr)
                                 query=&PvrSetup.HDPVR_AudioEncoding.queryctrl;
                               else
                                 query=&PvrSetup.AudioEncoding.queryctrl;
                               break;
  // Video
  case V4L2_CID_MPEG_VIDEO_BITRATE:
                               query=&PvrSetup.VideoBitrateTV.queryctrl;
                               break;
  case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
                               query=&PvrSetup.VideoBitratePeak.queryctrl;
                               break;
  case V4L2_CID_MPEG_VIDEO_ASPECT:
                               query=&PvrSetup.AspectRatio.queryctrl;
                               break;
  // MPEG
  case V4L2_CID_MPEG_STREAM_TYPE:
                               query=&PvrSetup.StreamType.queryctrl;
                               break;
  case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
                               query=&PvrSetup.BitrateMode.queryctrl;
                               break;
  case V4L2_CID_MPEG_VIDEO_B_FRAMES:
                               query=&PvrSetup.BFrames.queryctrl;
                               break;
  case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
                               query=&PvrSetup.GopSize.queryctrl;
                               break;
  case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
                               query=&PvrSetup.GopClosure.queryctrl;
                               break;
  // Video Filters
  case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
                               query=&PvrSetup.FilterSpatialMode.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
                               query=&PvrSetup.FilterSpatial.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
                               query=&PvrSetup.FilterLumaSpatialType.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
                               query=&PvrSetup.FilterChromaSpatialType.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
                               query=&PvrSetup.FilterTemporalMode.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER:
                               query=&PvrSetup.FilterTemporal.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
                               query=&PvrSetup.FilterMedianType.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM:
                               query=&PvrSetup.FilterLumaMedianBottom.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
                               query=&PvrSetup.FilterLumaMedianTop.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM:
                               query=&PvrSetup.FilterChromaMedianBottom.queryctrl;
                               break;
  case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP:
                               query=&PvrSetup.FilterChromaMedianTop.queryctrl;
                               break;
  if (SupportsSlicedVBI) {
    // VBI
    case V4L2_CID_MPEG_STREAM_VBI_FMT:
                                 query=&PvrSetup.VBIformat.queryctrl;
                                 break;
    }
  default:
    log(pvrERROR, "SetControlValue(__u32 control, __s32 Val): ERROR: control %d not included in switch(control)",\
       control);
    return -1;
  }

  return SetControlValue(ctrl_class, control, Val, *query);
}


/* 
Sets a V4L2 control.
If the value Val is out of range, the ivtv default value will be
used. The value set to V4L2 will be returned, either Val or the
default.
Has to be used *after* calling cPvrDevice::QueryAllControls().
-wirbel-
*/

int cPvrDevice::SetControlValue(__u32 control_class, __u32 control, __s32 Val, \
                                struct v4l2_queryctrl queryctrl) {

  struct v4l2_ext_controls ctrls;
  struct v4l2_ext_control  ctrl;

  if (!ControlIdIsValid(queryctrl.id)) // skip known unsupported controls of the driver with "no error"
    return Val;
  log (3, "SetControlValue(%s, %d)",queryctrl.name, Val);
  if ((queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) == V4L2_CTRL_FLAG_DISABLED){
    log(pvrINFO, " Error: this control is disabled by driver. Skipped.");
    return Val;
    }    
  if ((Val>queryctrl.maximum) || (Val<queryctrl.minimum)) {
    log(pvrINFO, "  Info: setting value to default(%d)",queryctrl.default_value);
    Val=queryctrl.default_value;
    }
  ctrl.id    = control;
  ctrl.value = Val;

  ctrls.ctrl_class=control_class;
  ctrls.controls=&ctrl;
  ctrls.count=1;

  if (IOCTL(v4l2_fd, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
    log(pvrERROR, "cPvrDevice::SetControlValue(): setting control value %s: %d:%s",
        queryctrl.name, errno, strerror(errno));
      }
  return Val;
}


/*
Queries the properties of a given valSet
20070510
-> Before using function, valSet's queryctrl.id has to be set.
-wirbel-
*/

int cPvrDevice::QueryControl(struct valSet * vs) {
  if (!ControlIdIsValid(vs->queryctrl.id)) // skip known unsupported controls of the driver with "no error"
    return 0;
  vs->query_isvalid = false;
  if (vs->queryctrl.id == 0) {
    log(pvrERROR, "cPvrDevice::QueryControl(): valSet not initialized.");
    return -1;
    }
  if (IOCTL(v4l2_fd, VIDIOC_QUERYCTRL, &vs->queryctrl) != 0) {
    log(pvrERROR, "QueryControl(): quering control %d failed.", vs->queryctrl.id);
    return -1;
    }
  vs->query_isvalid = true;
  return 0;
}


/*
Queries the properties of all valSets used in SetControlValue
20070510
-wirbel-
*/

bool cPvrDevice::QueryAllControls(void) {
  int err=0;
  log(pvrDEBUG1, "QueryAllControls");

  PvrSetup.Brightness.ctrl_class                = V4L2_CTRL_CLASS_USER;
  PvrSetup.Contrast.ctrl_class                  = V4L2_CTRL_CLASS_USER;
  PvrSetup.Saturation.ctrl_class                = V4L2_CTRL_CLASS_USER;
  PvrSetup.Hue.ctrl_class                       = V4L2_CTRL_CLASS_USER;
  // Audio
  PvrSetup.AudioVolumeTVCommon.ctrl_class       = V4L2_CTRL_CLASS_USER;
  PvrSetup.AudioVolumeTVException.ctrl_class    = V4L2_CTRL_CLASS_USER;
  PvrSetup.AudioVolumeFM.ctrl_class             = V4L2_CTRL_CLASS_USER;
  PvrSetup.AudioMute.ctrl_class                 = V4L2_CTRL_CLASS_USER;
  PvrSetup.AudioBitrate.ctrl_class              = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.AudioSampling.ctrl_class             = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.AudioEncoding.ctrl_class             = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.HDPVR_AudioEncoding.ctrl_class       = V4L2_CTRL_CLASS_MPEG;
  // Video
  PvrSetup.VideoBitrateTV.ctrl_class            = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.VideoBitratePeak.ctrl_class          = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.AspectRatio.ctrl_class               = V4L2_CTRL_CLASS_MPEG;
  // MPEG
  PvrSetup.StreamType.ctrl_class                = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.BitrateMode.ctrl_class               = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.BFrames.ctrl_class                   = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.GopSize.ctrl_class                   = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.GopClosure.ctrl_class                = V4L2_CTRL_CLASS_MPEG;
  // Video Filters
  PvrSetup.FilterSpatialMode.ctrl_class         = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterSpatial.ctrl_class             = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterLumaSpatialType.ctrl_class     = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterChromaSpatialType.ctrl_class   = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterTemporalMode.ctrl_class        = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterTemporal.ctrl_class            = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterMedianType.ctrl_class          = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterLumaMedianBottom.ctrl_class    = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterLumaMedianTop.ctrl_class       = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterChromaMedianBottom.ctrl_class  = V4L2_CTRL_CLASS_MPEG;
  PvrSetup.FilterChromaMedianTop.ctrl_class     = V4L2_CTRL_CLASS_MPEG;

  PvrSetup.Brightness.queryctrl.id              = V4L2_CID_BRIGHTNESS;
  PvrSetup.Contrast.queryctrl.id                = V4L2_CID_CONTRAST;
  PvrSetup.Saturation.queryctrl.id              = V4L2_CID_SATURATION;
  PvrSetup.Hue.queryctrl.id                     = V4L2_CID_HUE;
  // Audio
  PvrSetup.AudioVolumeTVCommon.queryctrl.id     = V4L2_CID_AUDIO_VOLUME;
  PvrSetup.AudioVolumeTVException.queryctrl.id  = V4L2_CID_AUDIO_VOLUME;
  PvrSetup.AudioVolumeFM.queryctrl.id           = V4L2_CID_AUDIO_VOLUME;
  PvrSetup.AudioMute.queryctrl.id               = V4L2_CID_AUDIO_MUTE;
  PvrSetup.AudioBitrate.queryctrl.id            = V4L2_CID_MPEG_AUDIO_L2_BITRATE;
  PvrSetup.AudioSampling.queryctrl.id           = V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ;
  PvrSetup.AudioEncoding.queryctrl.id           = V4L2_CID_MPEG_AUDIO_ENCODING;
  PvrSetup.HDPVR_AudioEncoding.queryctrl.id     = V4L2_CID_MPEG_AUDIO_ENCODING;
  // Video
  PvrSetup.VideoBitrateTV.queryctrl.id          = V4L2_CID_MPEG_VIDEO_BITRATE;
  PvrSetup.VideoBitratePeak.queryctrl.id        = V4L2_CID_MPEG_VIDEO_BITRATE_PEAK;
  PvrSetup.AspectRatio.queryctrl.id             = V4L2_CID_MPEG_VIDEO_ASPECT;
  // MPEG
  PvrSetup.StreamType.queryctrl.id              = V4L2_CID_MPEG_STREAM_TYPE;
  PvrSetup.BitrateMode.queryctrl.id             = V4L2_CID_MPEG_VIDEO_BITRATE_MODE;
  PvrSetup.BFrames.queryctrl.id                 = V4L2_CID_MPEG_VIDEO_B_FRAMES;
  PvrSetup.GopSize.queryctrl.id                 = V4L2_CID_MPEG_VIDEO_GOP_SIZE;
  PvrSetup.GopClosure.queryctrl.id              = V4L2_CID_MPEG_VIDEO_GOP_CLOSURE;
  // Video Filters
  PvrSetup.FilterSpatialMode.queryctrl.id       = V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE;
  PvrSetup.FilterSpatial.queryctrl.id           = V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER;
  PvrSetup.FilterLumaSpatialType.queryctrl.id   = V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE;
  PvrSetup.FilterChromaSpatialType.queryctrl.id = V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE;
  PvrSetup.FilterTemporalMode.queryctrl.id      = V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE;
  PvrSetup.FilterTemporal.queryctrl.id          = V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER;
  PvrSetup.FilterMedianType.queryctrl.id        = V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE;
  PvrSetup.FilterLumaMedianBottom.queryctrl.id  = V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM;
  PvrSetup.FilterLumaMedianTop.queryctrl.id     = V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP;
  PvrSetup.FilterChromaMedianBottom.queryctrl.id= V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM;
  PvrSetup.FilterChromaMedianTop.queryctrl.id   = V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP;
  if (SupportsSlicedVBI) {
    // VBI
    PvrSetup.VBIformat.queryctrl.id             = V4L2_CID_MPEG_STREAM_VBI_FMT;
    }
  /* now quering min, max, default */
  // picture properties
  err += QueryControl(&PvrSetup.Brightness);
  err += QueryControl(&PvrSetup.Contrast);
  err += QueryControl(&PvrSetup.Saturation);
  err += QueryControl(&PvrSetup.Hue);
  // Audio
  err += QueryControl(&PvrSetup.AudioVolumeTVCommon);
  err += QueryControl(&PvrSetup.AudioVolumeTVException);
  err += QueryControl(&PvrSetup.AudioVolumeFM);
  err += QueryControl(&PvrSetup.AudioMute);
  err += QueryControl(&PvrSetup.AudioBitrate);
  err += QueryControl(&PvrSetup.AudioSampling);
  if (driver == hdpvr)
    err += QueryControl(&PvrSetup.HDPVR_AudioEncoding);
  else
    err += QueryControl(&PvrSetup.AudioEncoding);
  // Video
  err += QueryControl(&PvrSetup.VideoBitrateTV);
  err += QueryControl(&PvrSetup.VideoBitratePeak);
  err += QueryControl(&PvrSetup.AspectRatio);
  // MPEG
  err += QueryControl(&PvrSetup.StreamType);
  err += QueryControl(&PvrSetup.BitrateMode);
  err += QueryControl(&PvrSetup.BFrames);
  err += QueryControl(&PvrSetup.GopSize);
  err += QueryControl(&PvrSetup.GopClosure);
  // Video Filters
  err += QueryControl(&PvrSetup.FilterSpatialMode);
  err += QueryControl(&PvrSetup.FilterSpatial);
  err += QueryControl(&PvrSetup.FilterLumaSpatialType);
  err += QueryControl(&PvrSetup.FilterChromaSpatialType);
  err += QueryControl(&PvrSetup.FilterTemporalMode);
  err += QueryControl(&PvrSetup.FilterTemporal);
  err += QueryControl(&PvrSetup.FilterMedianType);
  err += QueryControl(&PvrSetup.FilterLumaMedianBottom);
  err += QueryControl(&PvrSetup.FilterLumaMedianTop);
  err += QueryControl(&PvrSetup.FilterChromaMedianBottom);
  err += QueryControl(&PvrSetup.FilterChromaMedianTop);
  if (SupportsSlicedVBI) {
    // VBI
    err += QueryControl(&PvrSetup.VBIformat);
    }
  if (err)
    return false;

  // The following code checks wether a valSet.value is 
  // INVALID_VALUE and if so -> set it to its queryctrl.default_value.
  // The macro INIT(v) is a abbreviation for the 'if .. then' comparison.
  #define INIT(v) if (v.value == INVALID_VALUE) v.value=v.queryctrl.default_value
  // picture properties
  INIT(PvrSetup.Brightness);
  INIT(PvrSetup.Contrast);
  INIT(PvrSetup.Saturation);
  INIT(PvrSetup.Hue);
  // Audio
  if (PvrSetup.AudioVolumeTVCommon.value == INVALID_VALUE)
    PvrSetup.AudioVolumeTVCommon.value = \
         (int) (0.95 * PvrSetup.AudioVolumeTVCommon.queryctrl.maximum);
  if (PvrSetup.AudioVolumeTVException.value == INVALID_VALUE)
    PvrSetup.AudioVolumeTVException.value = \
         (int) (0.95 * PvrSetup.AudioVolumeTVException.queryctrl.maximum);
  if (PvrSetup.AudioVolumeFM.value == INVALID_VALUE)
    PvrSetup.AudioVolumeFM.value = \
         PvrSetup.AudioVolumeFM.queryctrl.maximum;
  INIT(PvrSetup.AudioBitrate);
  INIT(PvrSetup.AudioSampling);
  if (driver == hdpvr)
    INIT(PvrSetup.HDPVR_AudioEncoding);
  // Video
  INIT(PvrSetup.VideoBitrateTV);
  INIT(PvrSetup.AspectRatio);
  // MPEG
  INIT(PvrSetup.BitrateMode);
  INIT(PvrSetup.BFrames);
  INIT(PvrSetup.GopSize);
  INIT(PvrSetup.GopClosure);
  // Video Filters
  INIT(PvrSetup.FilterSpatialMode);
  INIT(PvrSetup.FilterSpatial);
  INIT(PvrSetup.FilterLumaSpatialType);
  INIT(PvrSetup.FilterChromaSpatialType);
  INIT(PvrSetup.FilterTemporalMode);
  INIT(PvrSetup.FilterTemporal);
  INIT(PvrSetup.FilterMedianType);
  INIT(PvrSetup.FilterLumaMedianBottom);
  INIT(PvrSetup.FilterLumaMedianTop);
  INIT(PvrSetup.FilterChromaMedianBottom);
  INIT(PvrSetup.FilterChromaMedianTop);
  return true;
}

