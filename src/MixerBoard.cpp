/**********************************************************************

  Audacity: A Digital Audio Editor

  MixerBoard.cpp

  Vaughan Johnson, January 2007
  Dominic Mazzoni

**********************************************************************/

#include "Audacity.h"

#include <cfloat>
#include <math.h>

#include <wx/dcmemory.h>
#include <wx/arrimpl.cpp>
#include <wx/icon.h>
#include <wx/settings.h> // for wxSystemSettings::GetColour and wxSystemSettings::GetMetric

#include "Theme.h"
#include "Experimental.h"
#include "MixerBoard.h"
#include "AColor.h"
#include "AllThemeResources.h"
#include "AudioIO.h"

#ifdef USE_MIDI
#include "NoteTrack.h"
#endif

#include "Project.h"
#include "TrackPanel.h" // for EVT_TRACK_PANEL_TIMER
#include "UndoManager.h"
#include "WaveTrack.h"

#include "widgets/Meter.h"


#include "../images/MusicalInstruments.h"
#ifdef __WXMSW__
   #include "../images/AudacityLogo.xpm"
#else
   #include "../images/AudacityLogo48x48.xpm"
#endif


// class MixerTrackSlider

BEGIN_EVENT_TABLE(MixerTrackSlider, ASlider)
   EVT_MOUSE_EVENTS(MixerTrackSlider::OnMouseEvent)

   EVT_SET_FOCUS(MixerTrackSlider::OnFocus)
   EVT_KILL_FOCUS(MixerTrackSlider::OnFocus)
   EVT_COMMAND(wxID_ANY, EVT_CAPTURE_KEY, MixerTrackSlider::OnCaptureKey)

END_EVENT_TABLE()

MixerTrackSlider::MixerTrackSlider(wxWindow * parent,
                                    wxWindowID id,
                                    const wxString &name,
                                    const wxPoint & pos,
                                    const wxSize & size,
                                    int style /*= FRAC_SLIDER*/,
                                    bool popup /*= true*/,
                                    bool canUseShift /*= true*/,
                                    float stepValue /*= STEP_CONTINUOUS*/,
                                    int orientation /*= wxHORIZONTAL*/)
: ASlider(parent, id, name, pos, size,
            style, popup, canUseShift, stepValue, orientation)
{
}

void MixerTrackSlider::OnMouseEvent(wxMouseEvent &event)
{
   ASlider::OnMouseEvent(event);

   if (event.ButtonUp())
   {
      MixerTrackCluster* pMixerTrackCluster = (MixerTrackCluster*)(this->GetParent());
      switch (mStyle)
      {
      case DB_SLIDER: pMixerTrackCluster->HandleSliderGain(true); break;
      case PAN_SLIDER: pMixerTrackCluster->HandleSliderPan(true); break;
      default: break; // no-op
      }
   }
}

void MixerTrackSlider::OnFocus(wxFocusEvent &event)
{
   if (event.GetEventType() == wxEVT_KILL_FOCUS) {
      AudacityProject::ReleaseKeyboard(this);
   }
   else {
      AudacityProject::CaptureKeyboard(this);
   }

   Refresh(false);

   event.Skip();
}

void MixerTrackSlider::OnCaptureKey(wxCommandEvent &event)
{
   wxKeyEvent *kevent = (wxKeyEvent *)event.GetEventObject();
   int keyCode = kevent->GetKeyCode();

   // Pass LEFT/RIGHT/UP/DOWN/PAGEUP/PAGEDOWN through for input/output sliders
   if (keyCode == WXK_LEFT || keyCode == WXK_RIGHT ||
       keyCode == WXK_UP || keyCode == WXK_DOWN ||
       keyCode == WXK_PAGEUP || keyCode == WXK_PAGEDOWN) {
      return;
   }

   event.Skip();

   return;
}



// class MixerTrackCluster

#define kInset             4
#define kDoubleInset       (2 * kInset)
#define kTripleInset       (3 * kInset)
#define kQuadrupleInset    (4 * kInset)

#define TRACK_NAME_HEIGHT                    18
#define MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH  48
#define MUTE_SOLO_HEIGHT                     19
#define PAN_HEIGHT                           24

#define kLeftSideStackWidth         MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH - kDoubleInset //vvv Change when numbers shown on slider scale.
#define kRightSideStackWidth        MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH + kDoubleInset
#define kMixerTrackClusterWidth     kLeftSideStackWidth + kRightSideStackWidth + kQuadrupleInset // kDoubleInset margin on both sides

enum {
   ID_BITMAPBUTTON_MUSICAL_INSTRUMENT = 13000,
   ID_SLIDER_PAN,
   ID_SLIDER_GAIN,
#ifdef EXPERIMENTAL_MIDI_OUT
   ID_SLIDER_VELOCITY,
#endif
   ID_TOGGLEBUTTON_MUTE,
   ID_TOGGLEBUTTON_SOLO,
};

BEGIN_EVENT_TABLE(MixerTrackCluster, wxPanelWrapper)
   EVT_MOUSE_EVENTS(MixerTrackCluster::OnMouseEvent)
   EVT_PAINT(MixerTrackCluster::OnPaint)

   EVT_BUTTON(ID_BITMAPBUTTON_MUSICAL_INSTRUMENT, MixerTrackCluster::OnButton_MusicalInstrument)
   EVT_SLIDER(ID_SLIDER_PAN, MixerTrackCluster::OnSlider_Pan)
   EVT_SLIDER(ID_SLIDER_GAIN, MixerTrackCluster::OnSlider_Gain)
#ifdef EXPERIMENTAL_MIDI_OUT
   EVT_SLIDER(ID_SLIDER_VELOCITY, MixerTrackCluster::OnSlider_Velocity)
#endif
   //v EVT_COMMAND_SCROLL(ID_SLIDER_GAIN, MixerTrackCluster::OnSliderScroll_Gain)
   EVT_COMMAND(ID_TOGGLEBUTTON_MUTE, wxEVT_COMMAND_BUTTON_CLICKED, MixerTrackCluster::OnButton_Mute)
   EVT_COMMAND(ID_TOGGLEBUTTON_SOLO, wxEVT_COMMAND_BUTTON_CLICKED, MixerTrackCluster::OnButton_Solo)
END_EVENT_TABLE()

MixerTrackCluster::MixerTrackCluster(wxWindow* parent,
                                       MixerBoard* grandParent, AudacityProject* project,
                                       PlayableTrack* pTrack,
                                       const wxPoint& pos /*= wxDefaultPosition*/,
                                       const wxSize& size /*= wxDefaultSize*/)
: wxPanelWrapper(parent, -1, pos, size)
, mTrack{ pTrack }
{
   mMixerBoard = grandParent;
   mProject = project;
   wxASSERT( pTrack );

   SetName(mTrack->GetName());

   //this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
   this->SetBackgroundColour( theTheme.Colour( clrMedium ) );
   // Not sure why, but sizers weren't getting offset vertically,
   // probably because not using wxDefaultPosition,
   // so positions are calculated explicitly below, and sizers code was removed.
   // (Still available in Audacity_UmixIt branch off 1.2.6.)

   // track name
   wxPoint ctrlPos(kDoubleInset, kDoubleInset);
   wxSize ctrlSize(size.GetWidth() - kQuadrupleInset, TRACK_NAME_HEIGHT);
   mStaticText_TrackName =
      safenew auStaticText(this, mTrack->GetName());
   //v Useful when different tracks are different colors, but not now.
   //    mStaticText_TrackName->SetBackgroundColour(this->GetTrackColor());
   mStaticText_TrackName->SetForegroundColour(theTheme.Colour(clrMedium));
   mStaticText_TrackName->SetForegroundColour(theTheme.Colour(clrTrackPanelText));
   mStaticText_TrackName->SetSize( ctrlSize );
   mStaticText_TrackName->SetPosition( ctrlPos );


   // gain and velocity sliders at left (both in same place)
   ctrlPos.x = kDoubleInset;
   ctrlPos.y += TRACK_NAME_HEIGHT + kDoubleInset;
   const int nGainSliderHeight =
      size.GetHeight() - ctrlPos.y - kQuadrupleInset;
   ctrlSize.Set(kLeftSideStackWidth - kQuadrupleInset, nGainSliderHeight);

   mSlider_Gain =
      safenew MixerTrackSlider(
            this, ID_SLIDER_GAIN,
            /* i18n-hint: title of the Gain slider, used to adjust the volume */
            _("Gain"),
            ctrlPos, ctrlSize, DB_SLIDER, true,
            true, 0.0, wxVERTICAL);
   mSlider_Gain->SetName(_("Gain"));
   this->UpdateGain();
#ifdef EXPERIMENTAL_MIDI_OUT
   mSlider_Velocity =
      safenew MixerTrackSlider(
            this, ID_SLIDER_VELOCITY,
            /* i18n-hint: title of the MIDI Velocity slider */
            _("Velocity"),
            ctrlPos, ctrlSize, VEL_SLIDER, true,
            true, 0.0, wxVERTICAL);
   mSlider_Velocity->SetName(_("Velocity"));
   this->UpdateVelocity();
#endif

   // other controls and meter at right

   // musical instrument image
   ctrlPos.x += kLeftSideStackWidth + kInset; // + kInset to center it in right side stack
   ctrlSize.Set(MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH, MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH);
   wxBitmap* bitmap = mMixerBoard->GetMusicalInstrumentBitmap(mTrack);
   wxASSERT(bitmap);
   mBitmapButton_MusicalInstrument =
      safenew wxBitmapButton(this, ID_BITMAPBUTTON_MUSICAL_INSTRUMENT, *bitmap,
                           ctrlPos, ctrlSize,
                           wxBU_AUTODRAW, wxDefaultValidator,
                           _("Musical Instrument"));
   mBitmapButton_MusicalInstrument->SetName(_("Musical Instrument"));


   // pan slider
   ctrlPos.x -= kInset; // Remove inset for instrument, so Pan is at leftmost of left side stack.
   ctrlPos.y += MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH + kDoubleInset;
   ctrlSize.Set(kRightSideStackWidth, PAN_HEIGHT);

   // The width of the pan slider must be odd (don't ask).
   if (!(ctrlSize.x & 1))
      ctrlSize.x--;

   mSlider_Pan =
      safenew MixerTrackSlider(
            this, ID_SLIDER_PAN,
            /* i18n-hint: Title of the Pan slider, used to move the sound left or right */
            _("Pan"),
            ctrlPos, ctrlSize, PAN_SLIDER, true);
   mSlider_Pan->SetName(_("Pan"));

   this->UpdatePan();

   // mute/solo buttons stacked below Pan slider
   ctrlPos.y += PAN_HEIGHT + kDoubleInset;
   ctrlSize.Set(mMixerBoard->mMuteSoloWidth, MUTE_SOLO_HEIGHT);
   mToggleButton_Mute =
      safenew AButton(this, ID_TOGGLEBUTTON_MUTE,
                  ctrlPos, ctrlSize,
                  *(mMixerBoard->mImageMuteUp), *(mMixerBoard->mImageMuteOver),
                  *(mMixerBoard->mImageMuteDown), *(mMixerBoard->mImageMuteDown),
                  *(mMixerBoard->mImageMuteDisabled),
                  true); // toggle button
   mToggleButton_Mute->SetName(_("Mute"));
   mToggleButton_Mute->SetAlternateImages(
      1,
      *(mMixerBoard->mImageMuteUp), *(mMixerBoard->mImageMuteOver),
      *(mMixerBoard->mImageMuteDown), *(mMixerBoard->mImageMuteDown), 
      *(mMixerBoard->mImageMuteDisabled));
   this->UpdateMute();

   ctrlPos.y += MUTE_SOLO_HEIGHT;
   mToggleButton_Solo =
      safenew AButton(this, ID_TOGGLEBUTTON_SOLO,
                  ctrlPos, ctrlSize,
                  *(mMixerBoard->mImageSoloUp), *(mMixerBoard->mImageSoloOver),
                  *(mMixerBoard->mImageSoloDown), *(mMixerBoard->mImageSoloDown), 
                  *(mMixerBoard->mImageSoloDisabled),
                  true); // toggle button
   mToggleButton_Solo->SetName(_("Solo"));
   this->UpdateSolo();
   bool bSoloNone = mProject->IsSoloNone();
   mToggleButton_Solo->Show(!bSoloNone);


   // meter
   ctrlPos.y += (bSoloNone ? 0 : MUTE_SOLO_HEIGHT) + kDoubleInset;
   const int nMeterHeight =
      nGainSliderHeight -
      (MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH + kDoubleInset) -
      (PAN_HEIGHT + kDoubleInset) -
      (MUTE_SOLO_HEIGHT + (bSoloNone ? 0 : MUTE_SOLO_HEIGHT) + kDoubleInset);
   ctrlSize.Set(kRightSideStackWidth, nMeterHeight);

   mMeter = NULL;
   if (GetWave()) {
      mMeter =
         safenew Meter(GetActiveProject(), // AudacityProject* project,
                   this, -1, // wxWindow* parent, wxWindowID id,
                   false, // bool isInput
                   ctrlPos, ctrlSize, // const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
                   Meter::MixerTrackCluster); // Style style = HorizontalStereo,
      mMeter->SetName(_("Signal Level Meter"));
   }

   #if wxUSE_TOOLTIPS
      mStaticText_TrackName->SetToolTip(mTrack->GetName());
      mToggleButton_Mute->SetToolTip(_("Mute"));
      mToggleButton_Solo->SetToolTip(_("Solo"));
      if (GetWave())
         mMeter->SetToolTip(_("Signal Level Meter"));
   #endif // wxUSE_TOOLTIPS

   #ifdef __WXMAC__
      wxSizeEvent event(GetSize(), GetId());
      event.SetEventObject(this);
      GetEventHandler()->ProcessEvent(event);
      UpdateGain();
   #endif
}

WaveTrack *MixerTrackCluster::GetWave() const
{
   return dynamic_cast< WaveTrack * >( mTrack );
}

WaveTrack *MixerTrackCluster::GetRight() const
{
   auto left = GetWave();
   if (left)
      return static_cast<WaveTrack*>(left->GetLink());
   else
      return nullptr;
}

#ifdef EXPERIMENTAL_MIDI_OUT
NoteTrack *MixerTrackCluster::GetNote() const
{
   return dynamic_cast< NoteTrack * >( mTrack );
}
#endif

void MixerTrackCluster::UpdatePrefs()
{
   this->SetBackgroundColour( theTheme.Colour( clrMedium ) );
   mStaticText_TrackName->SetForegroundColour(theTheme.Colour(clrTrackPanelText));
   if (mMeter)
      mMeter->UpdatePrefs(); // in case meter range has changed
   HandleResize(); // in case prefs "/GUI/Solo" changed
}

void MixerTrackCluster::HandleResize() // For wxSizeEvents, update gain slider and meter.
{
   wxSize scrolledWindowClientSize = this->GetParent()->GetClientSize();
   const int newClusterHeight =
      scrolledWindowClientSize.GetHeight() - kDoubleInset - // nClusterHeight from MixerBoard::UpdateTrackClusters
      wxSystemSettings::GetMetric(wxSYS_HSCROLL_Y) + // wxScrolledWindow::GetClientSize doesn't account for its scrollbar size.
      kDoubleInset;

   this->SetSize(-1, newClusterHeight);

   // Change only the heights of mSlider_Gain and mMeter.
   // But update shown status of mToggleButton_Solo, which affects top of mMeter.
   const int nGainSliderHeight =
      newClusterHeight -
      (kInset + // margin above mStaticText_TrackName
         TRACK_NAME_HEIGHT + kDoubleInset) - // mStaticText_TrackName + margin
      kQuadrupleInset; // margin below gain slider
   mSlider_Gain->SetSize(-1, nGainSliderHeight);
#ifdef EXPERIMENTAL_MIDI_OUT
   mSlider_Velocity->SetSize(-1, nGainSliderHeight);
#endif

   bool bSoloNone = mProject->IsSoloNone();

   mToggleButton_Solo->Show(!bSoloNone);

   const int nRequiredHeightAboveMeter =
      MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH + kDoubleInset +
      PAN_HEIGHT + kDoubleInset +
      MUTE_SOLO_HEIGHT + (bSoloNone ? 0 : MUTE_SOLO_HEIGHT) + kDoubleInset;
   const int nMeterY =
      kDoubleInset + // margin at top
      TRACK_NAME_HEIGHT + kDoubleInset +
      nRequiredHeightAboveMeter;
   const int nMeterHeight = nGainSliderHeight - nRequiredHeightAboveMeter;
   if (mMeter)
      mMeter->SetSize(-1, nMeterY, -1, nMeterHeight);
}

void MixerTrackCluster::HandleSliderGain(const bool bWantPushState /*= false*/)
{
   float fValue = mSlider_Gain->Get();
   if (GetWave())
      GetWave()->SetGain(fValue);
   if (GetRight())
      GetRight()->SetGain(fValue);

   // Update the TrackPanel correspondingly.
   mProject->RefreshTPTrack(mTrack);
   if (bWantPushState)
      mProject->TP_PushState(_("Moved gain slider"), _("Gain"), UndoPush::CONSOLIDATE );
}

#ifdef EXPERIMENTAL_MIDI_OUT
void MixerTrackCluster::HandleSliderVelocity(const bool bWantPushState /*= false*/)
{
   float fValue = mSlider_Velocity->Get();
   if (GetNote())
      GetNote()->SetVelocity(fValue);

   // Update the TrackPanel correspondingly.
   mProject->RefreshTPTrack(mTrack);
   if (bWantPushState)
      mProject->TP_PushState(_("Moved velocity slider"), _("Velocity"), UndoPush::CONSOLIDATE);
}
#endif

void MixerTrackCluster::HandleSliderPan(const bool bWantPushState /*= false*/)
{
   float fValue = mSlider_Pan->Get();
   if (GetWave()) // test in case track is a NoteTrack
      GetWave()->SetPan(fValue);
   if (GetRight())
      GetRight()->SetPan(fValue);

   // Update the TrackPanel correspondingly.
   mProject->RefreshTPTrack(mTrack);

   if (bWantPushState)
      mProject->TP_PushState(_("Moved pan slider"), _("Pan"), UndoPush::CONSOLIDATE );
}

void MixerTrackCluster::ResetMeter(const bool bResetClipping)
{
   if (mMeter)
      mMeter->Reset(GetWave()->GetRate(), bResetClipping);
}


// These are used by TrackPanel for synchronizing control states, etc.

// Update the controls that can be affected by state change.
void MixerTrackCluster::UpdateForStateChange()
{
   this->UpdateName();
   this->UpdatePan();
   this->UpdateGain();
}

void MixerTrackCluster::UpdateName()
{
   const wxString newName = mTrack->GetName();
   SetName(newName);
   mStaticText_TrackName->SetLabel(newName);
   #if wxUSE_TOOLTIPS
      mStaticText_TrackName->SetToolTip(newName);
   #endif
   mBitmapButton_MusicalInstrument->SetBitmapLabel(
      *(mMixerBoard->GetMusicalInstrumentBitmap(mTrack)));
}

void MixerTrackCluster::UpdateMute()
{
   mToggleButton_Mute->SetAlternateIdx(mTrack->GetSolo() ? 1 : 0);
   if (mTrack->GetMute())
      mToggleButton_Mute->PushDown();
   else
      mToggleButton_Mute->PopUp();
}

void MixerTrackCluster::UpdateSolo()
{
   bool bIsSolo = mTrack->GetSolo();
   if (bIsSolo)
      mToggleButton_Solo->PushDown();
   else
      mToggleButton_Solo->PopUp();
   mToggleButton_Mute->SetAlternateIdx(bIsSolo ? 1 : 0);
}

void MixerTrackCluster::UpdatePan()
{
   if (!GetWave()) {
      mSlider_Pan->Hide();
      return;
   }
   mSlider_Pan->Set(GetWave()->GetPan());
}

void MixerTrackCluster::UpdateGain()
{
   if (!GetWave()) {
      mSlider_Gain->Hide();
      return;
   }
   mSlider_Gain->Set(GetWave()->GetGain());
}

#ifdef EXPERIMENTAL_MIDI_OUT
void MixerTrackCluster::UpdateVelocity()
{
   if (!GetNote()) {
      mSlider_Velocity->Hide();
      return;
   }
   mSlider_Velocity->Set(GetNote()->GetVelocity());
}
#endif

void MixerTrackCluster::UpdateMeter(const double t0, const double t1)
{
   // NoteTracks do not (currently) register on meters. It would probably be
   // a good idea to display 16 channel "active" lights rather than a meter
   if (!GetWave())
      return;

   if ((t0 < 0.0) || (t1 < 0.0) || (t0 >= t1) || // bad time value or nothing to show
         ((mMixerBoard->HasSolo() || mTrack->GetMute()) && !mTrack->GetSolo())
      )
   {
      //v Vaughan, 2011-02-25: Moved the update back to TrackPanel::OnTimer() as it helps with
      //    playback issues reported by Bill and noted on Bug 258, so no assert.
      // Vaughan, 2011-02-04: Now that we're updating all meters from audacityAudioCallback,
      //    this causes an assert if you click Mute while playing, because ResetMeter() resets
      //    the timer, and wxTimerbase says that can only be done from main thread --
      //    but it seems to work fine.
      this->ResetMeter(false);
      return;
   }

   // Vaughan, 2010-11-27:
   // This commented out code is flawed. Mistaken understanding of "frame" vs "window".
   // Caused me to override Meter::UpdateDisplay().
   // But I think it's got a good idea, of calling WaveTracks' GetMinMax and GetRMS
   // instead of passing in all the data and asking the meter to derive peak and rms.
   // May be worth revisiting as I think it should perform better, because it uses the min/max/rms
   // stored in blockfiles, rather than calculating them, but for now, changing it to use the
   // original Meter::UpdateDisplay(). New code is below the previous (now commented out).
   //
   //const size_t kFramesPerBuffer = 4;
   //float min; // dummy, since it's not shown in meters
   //Floats maxLeft{kFramesPerBuffer};
   //Floats rmsLeft{kFramesPerBuffer};
   //Floats maxRight{kFramesPerBuffer};
   //Floats rmsRight{kFramesPerBuffer};
   //
   //#ifdef EXPERIMENTAL_MIDI_OUT
   //   bool bSuccess = (GetWave() != nullptr);
   //#else
   //   bool bSuccess = true;
   //#endif

   //const double dFrameInterval = (t1 - t0) / (double)kFramesPerBuffer;
   //double dFrameT0 = t0;
   //double dFrameT1 = t0 + dFrameInterval;
   //int i = 0;
   //while (bSuccess && (i < kFramesPerBuffer))
   //{
   //   bSuccess &=
   //      mTrack->GetMinMax(&min, &(maxLeft[i]), dFrameT0, dFrameT1) &&
   //      mTrack->GetRMS(&(rmsLeft[i]), dFrameT0, dFrameT1);
   //   if (bSuccess && mRightTrack)
   //      bSuccess &=
   //         mRightTrack->GetMinMax(&min, &(maxRight[i]), dFrameT0, dFrameT1) &&
   //         mRightTrack->GetRMS(&(rmsRight[i]), dFrameT0, dFrameT1);
   //   else
   //   {
   //      // Mono: Start with raw values same as left.
   //      // To be modified by bWantPostFadeValues and channel pan/gain.
   //      maxRight[i] = maxLeft[i];
   //      rmsRight[i] = rmsLeft[i];
   //   }
   //   dFrameT0 += dFrameInterval;
   //   dFrameT1 += dFrameInterval;
   //   i++;
   //}
   //
   //const bool bWantPostFadeValues = true; //v Turn this into a checkbox on MixerBoard? For now, always true.
   //if (bSuccess && bWantPostFadeValues)
   //if (bSuccess)
   //{
   //   for (i = 0; i < kFramesPerBuffer; i++)
   //   {
   //      float gain = mTrack->GetChannelGain(0);
   //      maxLeft[i] *= gain;
   //      rmsLeft[i] *= gain;
   //      if (mRightTrack)
   //         gain = mRightTrack->GetChannelGain(1);
   //      maxRight[i] *= gain;
   //      rmsRight[i] *= gain;
   //   }
   //   if ( mMeter ) mMeter->UpdateDisplay(
   //      2, // If mono, show left track values in both meters, as in MeterToolBar, rather than kNumChannels.
   //      kFramesPerBuffer,
   //      maxLeft, rmsLeft,
   //      maxRight, rmsRight,
   //      mTrack->TimeToLongSamples(t1 - t0));
   //}
   //

   const auto pTrack = GetWave();
   auto startSample = (sampleCount)((pTrack->GetRate() * t0) + 0.5);
   auto scnFrames = (sampleCount)((pTrack->GetRate() * (t1 - t0)) + 0.5);

   // Expect that the difference of t1 and t0 is the part of a track played
   // in about 1/20 second (ticks of TrackPanel timer), so this won't overflow
   auto nFrames = scnFrames.as_size_t();

   Floats tempFloatsArray{ nFrames };
   decltype(tempFloatsArray) meterFloatsArray;
   // Don't throw on read error in this drawing update routine
   bool bSuccess = pTrack->Get((samplePtr)tempFloatsArray.get(),
      floatSample, startSample, nFrames, fillZero, false);
   if (bSuccess)
   {
      // We always pass a stereo sample array to the meter, as it shows 2 channels.
      // Mono shows same in both meters.
      // Since we're not mixing, need to duplicate same signal for "right" channel in mono case.
      meterFloatsArray = Floats{ 2 * nFrames };

      // Interleave for stereo. Left/mono first.
      for (unsigned int index = 0; index < nFrames; index++)
         meterFloatsArray[2 * index] = tempFloatsArray[index];

      if (GetRight())
         // Again, don't throw
         bSuccess = GetRight()->Get((samplePtr)tempFloatsArray.get(),
            floatSample, startSample, nFrames, fillZero, false);

      if (bSuccess)
         // Interleave right channel, or duplicate same signal for "right" channel in mono case.
         for (unsigned int index = 0; index < nFrames; index++)
            meterFloatsArray[(2 * index) + 1] = tempFloatsArray[index];
   }

   //const bool bWantPostFadeValues = true; //v Turn this into a checkbox on MixerBoard? For now, always true.
   //if (bSuccess && bWantPostFadeValues)
   if (bSuccess)
   {
      //vvv Need to apply envelope, too? See Mixer::MixSameRate.
      float gain = pTrack->GetChannelGain(0);
      if (gain < 1.0)
         for (unsigned int index = 0; index < nFrames; index++)
            meterFloatsArray[2 * index] *= gain;
      if (GetRight())
         gain = GetRight()->GetChannelGain(1);
      else
         gain = pTrack->GetChannelGain(1);
      if (gain < 1.0)
         for (unsigned int index = 0; index < nFrames; index++)
            meterFloatsArray[(2 * index) + 1] *= gain;
      // Clip to [-1.0, 1.0] range.
      for (unsigned int index = 0; index < 2 * nFrames; index++)
         if (meterFloatsArray[index] < -1.0)
            meterFloatsArray[index] = -1.0;
         else if (meterFloatsArray[index] > 1.0)
            meterFloatsArray[index] = 1.0;

      mMeter->UpdateDisplay(2, nFrames, meterFloatsArray.get());
   }
   else
      this->ResetMeter(false);
}

// private

wxColour MixerTrackCluster::GetTrackColor()
{
   return wxColour(102, 255, 102); // same as Meter playback color
}


// event handlers

void MixerTrackCluster::HandleSelect(bool bShiftDown, bool bControlDown)
{
   mProject->HandleListSelection( mTrack, bShiftDown, bControlDown, true
                                 );
}

void MixerTrackCluster::OnMouseEvent(wxMouseEvent& event)
{
   if (event.ButtonUp())
      this->HandleSelect(event.ShiftDown(), event.ControlDown());
   else
      event.Skip();
}

void MixerTrackCluster::OnPaint(wxPaintEvent & WXUNUSED(event))
{
   wxPaintDC dc(this);

   #ifdef __WXMAC__
      // Fill with correct color, not scroller background. Done automatically on Windows.
      AColor::Medium(&dc, false);
      dc.DrawRectangle(this->GetClientRect());
   #endif

   wxSize clusterSize = this->GetSize();
   wxRect bev(0, 0, clusterSize.GetWidth() - 1, clusterSize.GetHeight() - 1);

   auto selected = mTrack->GetSelected();

   for (unsigned int i = 0; i < 4; i++) // 4 gives a big bevel, but there were complaints about visibility otherwise.
   {
      bev.Inflate(-1, -1);
      AColor::Bevel(dc, !selected, bev);
   }
}


void MixerTrackCluster::OnButton_MusicalInstrument(wxCommandEvent& WXUNUSED(event))
{
   const auto &state = ::wxGetMouseState();
   this->HandleSelect(state.ShiftDown(), state.ControlDown());
}

void MixerTrackCluster::OnSlider_Gain(wxCommandEvent& WXUNUSED(event))
{
   this->HandleSliderGain();
}

#ifdef EXPERIMENTAL_MIDI_OUT
void MixerTrackCluster::OnSlider_Velocity(wxCommandEvent& WXUNUSED(event))
{
   this->HandleSliderVelocity();
}
#endif

//v void MixerTrackCluster::OnSliderScroll_Gain(wxScrollEvent& WXUNUSED(event))
//{
   //int sliderValue = (int)(mSlider_Gain->Get()); //v mSlider_Gain->GetValue();
   //#ifdef __WXMSW__
   //   // Negate because wxSlider on Windows has min at top, max at bottom.
   //   // mSlider_Gain->GetValue() is in [-6,36]. wxSlider has min at top, so this is [-36dB,6dB].
   //   sliderValue = -sliderValue;
   //#endif
   //wxString str = _("Gain: ");
   //if (sliderValue > 0)
   //   str += "+";
   //str += wxString::Format("%d dB", sliderValue);
   //mSlider_Gain->SetToolTip(str);
//}

void MixerTrackCluster::OnSlider_Pan(wxCommandEvent& WXUNUSED(event))
{
   this->HandleSliderPan();
}

void MixerTrackCluster::OnButton_Mute(wxCommandEvent& WXUNUSED(event))
{
   mProject->HandleTrackMute(mTrack, mToggleButton_Mute->WasShiftDown());
   mToggleButton_Mute->SetAlternateIdx(mTrack->GetSolo() ? 1 : 0);

   // Update the TrackPanel correspondingly.
   if (mProject->IsSoloSimple())
   {
      // Have to refresh all tracks.
      mMixerBoard->UpdateSolo();
      mProject->RedrawProject();
   }
   else
      // Update only the changed track.
      mProject->RefreshTPTrack(mTrack);
}

void MixerTrackCluster::OnButton_Solo(wxCommandEvent& WXUNUSED(event))
{
   mProject->HandleTrackSolo(mTrack, mToggleButton_Solo->WasShiftDown());
   bool bIsSolo = mTrack->GetSolo();
   mToggleButton_Mute->SetAlternateIdx(bIsSolo ? 1 : 0);

   // Update the TrackPanel correspondingly.
   if (mProject->IsSoloSimple())
   {
      // Have to refresh all tracks.
      mMixerBoard->UpdateMute();
      mMixerBoard->UpdateSolo();
   }
   // Bug 509: Must repaint all, as many tracks can change with one Solo change.
   mProject->RedrawProject();
}


// class MusicalInstrument

MusicalInstrument::MusicalInstrument(std::unique_ptr<wxBitmap> &&pBitmap, const wxString & strXPMfilename)
{
   mBitmap = std::move(pBitmap);

   int nUnderscoreIndex;
   wxString strFilename = strXPMfilename;
   strFilename.MakeLower(); // Make sure, so we don't have to do case insensitive comparison.
   wxString strKeyword;
   while ((nUnderscoreIndex = strFilename.Find(wxT('_'))) != -1)
   {
      strKeyword = strFilename.Left(nUnderscoreIndex);
      mKeywords.Add(strKeyword);
      strFilename = strFilename.Mid(nUnderscoreIndex + 1);
   }
   if (!strFilename.IsEmpty()) // Skip trailing underscores.
      mKeywords.Add(strFilename); // Add the last one.
}

MusicalInstrument::~MusicalInstrument()
{
   mKeywords.Clear();
}


// class MixerBoardScrolledWindow

// wxScrolledWindow ignores mouse clicks in client area,
// but they don't get passed to Mixerboard.
// We need to catch them to deselect all track clusters.

BEGIN_EVENT_TABLE(MixerBoardScrolledWindow, wxScrolledWindow)
   EVT_MOUSE_EVENTS(MixerBoardScrolledWindow::OnMouseEvent)
END_EVENT_TABLE()

MixerBoardScrolledWindow::MixerBoardScrolledWindow(AudacityProject* project,
                                                   MixerBoard* parent, wxWindowID id /*= -1*/,
                                                   const wxPoint& pos /*= wxDefaultPosition*/,
                                                   const wxSize& size /*= wxDefaultSize*/,
                                                   long style /*= wxHSCROLL | wxVSCROLL*/)
: wxScrolledWindow(parent, id, pos, size, style)
{
   mMixerBoard = parent;
   mProject = project;
}

MixerBoardScrolledWindow::~MixerBoardScrolledWindow()
{
}

void MixerBoardScrolledWindow::OnMouseEvent(wxMouseEvent& event)
{
   if (event.ButtonUp())
   {
      //v Even when I implement MixerBoard::OnMouseEvent and call event.Skip()
      // here, MixerBoard::OnMouseEvent never gets called.
      // So, added mProject to MixerBoardScrolledWindow and just directly do what's needed here.
      mProject->SelectNone();
   }
   else
      event.Skip();
}


// class MixerBoard

#define MIXER_BOARD_MIN_HEIGHT      460

// Min width is one cluster wide, plus margins.
#define MIXER_BOARD_MIN_WIDTH       kTripleInset + kMixerTrackClusterWidth + kTripleInset


BEGIN_EVENT_TABLE(MixerBoard, wxWindow)
   EVT_SIZE(MixerBoard::OnSize)
END_EVENT_TABLE()

MixerBoard::MixerBoard(AudacityProject* pProject,
                        wxFrame* parent,
                        const wxPoint& pos /*= wxDefaultPosition*/,
                        const wxSize& size /*= wxDefaultSize*/)
: wxWindow(parent, -1, pos, size)
{
   // public data members

   // mute & solo button images
   // Create once and store on MixerBoard for use in all MixerTrackClusters.
   mImageMuteUp = NULL;
   mImageMuteOver = NULL;
   mImageMuteDown = NULL;
   mImageMuteDownWhileSolo = NULL;
   mImageMuteDisabled = NULL;
   mImageSoloUp = NULL;
   mImageSoloOver = NULL;
   mImageSoloDown = NULL;
   mImageSoloDisabled = NULL;

   mMuteSoloWidth = kRightSideStackWidth - kInset; // correct for max width, but really set in MixerBoard::CreateMuteSoloImages

   // private data members
   this->LoadMusicalInstruments(); // Set up mMusicalInstruments.
   mProject = pProject;

   wxASSERT(pProject); // to justify safenew
   mScrolledWindow =
      safenew MixerBoardScrolledWindow(
         pProject, // AudacityProject* project,
         this, -1, // wxWindow* parent, wxWindowID id = -1,
         this->GetClientAreaOrigin(), // const wxPoint& pos = wxDefaultPosition,
         size, // const wxSize& size = wxDefaultSize,
         wxHSCROLL); // long style = wxHSCROLL | wxVSCROLL, const wxString& name = "scrolledWindow")

   // Set background color to same as TrackPanel background.
//   #ifdef EXPERIMENTAL_THEMING
//      mScrolledWindow->SetBackgroundColour(this->GetParent()->GetBackgroundColour());
//   #else
//      mScrolledWindow->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW));
//   #endif
   mScrolledWindow->SetBackgroundColour( theTheme.Colour( clrMedium ) );

   mScrolledWindow->SetScrollRate(10, 0); // no vertical scroll
   mScrolledWindow->SetVirtualSize(size);

   /* This doesn't work to make the mScrolledWindow automatically resize, so do it explicitly in OnSize.
         auto pBoxSizer = std::make_unique<wxBoxSizer>(wxVERTICAL);
         pBoxSizer->Add(mScrolledWindow, 0, wxExpand, 0);
         this->SetAutoLayout(true);
         this->SetSizer(pBoxSizer);
         pBoxSizer->Fit(this);
         pBoxSizer->SetSizeHints(this);
      */

   mPrevT1 = 0.0;
   mTracks = mProject->GetTracks();

   // Events from the project don't propagate directly to this other frame, so...
   mProject->Connect(EVT_TRACK_PANEL_TIMER,
      wxCommandEventHandler(MixerBoard::OnTimer),
      NULL,
      this);
}

MixerBoard::~MixerBoard()
{
   // private data members
   mMusicalInstruments.clear();

   mProject->Disconnect(EVT_TRACK_PANEL_TIMER,
      wxCommandEventHandler(MixerBoard::OnTimer),
      NULL,
      this);
}




void MixerBoard::UpdatePrefs()
{
   mProject->RecreateMixerBoard();

// Old approach modified things in situ.
// However with a theme change there is so much to modify, it is easier
// to recreate.
#if 0
   mScrolledWindow->SetBackgroundColour( theTheme.Colour( clrMedium ) );
   if( mImageMuteUp ){
      mImageMuteUp.reset();
      mImageMuteOver.reset();
      mImageMuteDown.reset();
      mImageMuteDownWhileSolo.reset();
      mImageMuteDisabled.reset();
      mImageSoloUp.reset();
      mImageSoloOver.reset();
      mImageSoloDown.reset();
      mImageSoloDisabled.reset();
   }
   for (unsigned int nClusterIndex = 0; nClusterIndex < mMixerTrackClusters.GetCount(); nClusterIndex++)
      mMixerTrackClusters[nClusterIndex]->UpdatePrefs();
   Refresh();
#endif
}

// Reassign mixer input strips (MixerTrackClusters) to Track Clusters
// both have the same order.
// If EXPERIMENTAL_MIDI_OUT, then Note Tracks appear in the
// mixer, and we must be able to convert and reuse a MixerTrackCluster
// from audio to midi or midi to audio. This task is handled by
// UpdateForStateChange().
//
void MixerBoard::UpdateTrackClusters()
{
   if (!mImageMuteUp)
      this->CreateMuteSoloImages();

   const int nClusterHeight = mScrolledWindow->GetClientSize().GetHeight() - kDoubleInset;
   size_t nClusterCount = mMixerTrackClusters.GetCount();
   unsigned int nClusterIndex = 0;
   TrackListIterator iterTracks(mTracks);
   MixerTrackCluster* pMixerTrackCluster = NULL;
   Track* pTrack;

   pTrack = iterTracks.First();
   while (pTrack) {
      if (auto pPlayableTrack = dynamic_cast<PlayableTrack*>(pTrack))
      {
         if (nClusterIndex < nClusterCount)
         {
            // Already showing it.
            // Track clusters are maintained in the same order as the WaveTracks.
            // Track pointers can change for the "same" track for different states
            // on the undo stack, so update the pointers and display name.
            mMixerTrackClusters[nClusterIndex]->mTrack = pPlayableTrack;
            // Assume linked track is wave or null
            mMixerTrackClusters[nClusterIndex]->UpdateForStateChange();
         }
         else
         {
            // Not already showing it. Add a NEW MixerTrackCluster.
            wxPoint clusterPos(
               (kInset +                                       // extra inset to left for first one, so it's double
                  (nClusterIndex *
                     (kInset + kMixerTrackClusterWidth)) +     // left margin and width for each to its left
                  kInset),                                     // plus left margin for NEW cluster
               kInset);
            wxSize clusterSize(kMixerTrackClusterWidth, nClusterHeight);
            pMixerTrackCluster =
               safenew MixerTrackCluster(mScrolledWindow, this, mProject,
                                       pPlayableTrack,
                                       clusterPos, clusterSize);
            if (pMixerTrackCluster)
               mMixerTrackClusters.Add(pMixerTrackCluster);
         }
         nClusterIndex++;
      }
      pTrack = iterTracks.Next(true);
   }

   if (pMixerTrackCluster)
   {
      // Added at least one MixerTrackCluster.
      this->UpdateWidth();
      this->ResizeTrackClusters();
   }
   else while (nClusterIndex < nClusterCount)
   {
      // We've got too many clusters.
      // This can happen only on things like Undo New Audio Track or Undo Import
      // that don't call RemoveTrackCluster explicitly.
      // We've already updated the track pointers for the clusters to the left, so just remove all the rest.
      // Successively DELETE from right to left.
      RemoveTrackCluster(--nClusterCount);
   }
}

int MixerBoard::GetTrackClustersWidth()
{
   return
      kInset +                                     // extra margin at left for first one
      (mMixerTrackClusters.GetCount() *            // number of tracks times
         (kInset + kMixerTrackClusterWidth)) +     // left margin and width for each
      kDoubleInset;                                // plus final right margin
}

void MixerBoard::MoveTrackCluster(const PlayableTrack* pTrack,
                                  bool bUp) // Up in TrackPanel is left in MixerBoard.
{
   MixerTrackCluster* pMixerTrackCluster;
   int nIndex = FindMixerTrackCluster(pTrack, &pMixerTrackCluster);
   if (pMixerTrackCluster == NULL)
      return; // Couldn't find it.

   wxPoint pos;
   if (bUp)
   {  // Move it up (left).
      if (nIndex <= 0)
         return; // It's already first (0), or not found (-1).

      pos = pMixerTrackCluster->GetPosition();
      mMixerTrackClusters[nIndex] = mMixerTrackClusters[nIndex - 1];
      mMixerTrackClusters[nIndex]->Move(pos);

      mMixerTrackClusters[nIndex - 1] = pMixerTrackCluster;
      pMixerTrackCluster->Move(pos.x - (kInset + kMixerTrackClusterWidth), pos.y);
   }
   else
   {  // Move it down (right).
      if (((unsigned int)nIndex + 1) >= mMixerTrackClusters.GetCount())
         return; // It's already last.

      pos = pMixerTrackCluster->GetPosition();
      mMixerTrackClusters[nIndex] = mMixerTrackClusters[nIndex + 1];
      mMixerTrackClusters[nIndex]->Move(pos);

      mMixerTrackClusters[nIndex + 1] = pMixerTrackCluster;
      pMixerTrackCluster->Move(pos.x + (kInset + kMixerTrackClusterWidth), pos.y);
   }
}

void MixerBoard::RemoveTrackCluster(const PlayableTrack* pTrack)
{
   // Find and destroy.
   MixerTrackCluster* pMixerTrackCluster;
   int nIndex = this->FindMixerTrackCluster(pTrack, &pMixerTrackCluster);

   if (pMixerTrackCluster == NULL)
      return; // Couldn't find it.

   RemoveTrackCluster(nIndex);
}

void MixerBoard::RemoveTrackCluster(size_t nIndex)
{
   auto pMixerTrackCluster = mMixerTrackClusters[nIndex];
   mMixerTrackClusters.RemoveAt(nIndex);
   pMixerTrackCluster->Destroy(); // DELETE is unsafe on wxWindow.

   // Close the gap, if any.
   wxPoint pos;
   int targetX;
   for (unsigned int i = nIndex; i < mMixerTrackClusters.GetCount(); i++)
   {
      pos = mMixerTrackClusters[i]->GetPosition();
      targetX =
         kInset +                                     // extra inset to left for first one, so it's double
         (i * (kInset + kMixerTrackClusterWidth)) +   // left margin and width for each
         kInset;                                      // plus left margin for this cluster
      if (pos.x != targetX)
         mMixerTrackClusters[i]->Move(targetX, pos.y);
   }

   this->UpdateWidth();
}


wxBitmap* MixerBoard::GetMusicalInstrumentBitmap(const Track* pTrack)
{
   if (mMusicalInstruments.empty())
      return NULL;

   // random choice:    return mMusicalInstruments[(int)pTrack % mMusicalInstruments.GetCount()].mBitmap;

   const wxString strTrackName(pTrack->GetName().MakeLower());
   size_t nBestItemIndex = 0;
   unsigned int nBestScore = 0;
   unsigned int nInstrIndex = 0;
   unsigned int nKeywordIndex;
   unsigned int nNumKeywords;
   unsigned int nPointsPerMatch;
   unsigned int nScore;
   for (nInstrIndex = 0; nInstrIndex < mMusicalInstruments.size(); nInstrIndex++)
   {
      nScore = 0;

      nNumKeywords = mMusicalInstruments[nInstrIndex]->mKeywords.GetCount();
      if (nNumKeywords > 0)
      {
         nPointsPerMatch = 10 / nNumKeywords;
         for (nKeywordIndex = 0; nKeywordIndex < nNumKeywords; nKeywordIndex++)
            if (strTrackName.Contains(mMusicalInstruments[nInstrIndex]->mKeywords[nKeywordIndex]))
            {
               nScore +=
                  nPointsPerMatch +
                  // Longer keywords get more points.
                  (2 * mMusicalInstruments[nInstrIndex]->mKeywords[nKeywordIndex].Length());
            }
      }

      // Choose later one if just matching nBestScore, for better variety,
      // and so default works as last element.
      if (nScore >= nBestScore)
      {
         nBestScore = nScore;
         nBestItemIndex = nInstrIndex;
      }
   }
   return mMusicalInstruments[nBestItemIndex]->mBitmap.get();
}

bool MixerBoard::HasSolo()
{
   TrackListIterator iterTracks(mTracks);
   Track* pTrack;
   for (pTrack = iterTracks.First(); pTrack; pTrack = iterTracks.Next()) {
      auto pPlayable = dynamic_cast<PlayableTrack *>( pTrack );
      if (pPlayable && pPlayable->GetSolo())
         return true;
   }
   return false;
}

void MixerBoard::RefreshTrackCluster(const PlayableTrack* pTrack, bool bEraseBackground /*= true*/)
{
   MixerTrackCluster* pMixerTrackCluster;
   this->FindMixerTrackCluster(pTrack, &pMixerTrackCluster);
   if (pMixerTrackCluster)
      pMixerTrackCluster->Refresh(bEraseBackground);
}

void MixerBoard::RefreshTrackClusters(bool bEraseBackground /*= true*/)
{
   for (unsigned int i = 0; i < mMixerTrackClusters.GetCount(); i++)
      mMixerTrackClusters[i]->Refresh(bEraseBackground);
}

void MixerBoard::ResizeTrackClusters()
{
   for (unsigned int nClusterIndex = 0; nClusterIndex < mMixerTrackClusters.GetCount(); nClusterIndex++)
      mMixerTrackClusters[nClusterIndex]->HandleResize();
}

void MixerBoard::ResetMeters(const bool bResetClipping)
{
   mPrevT1 = BAD_STREAM_TIME;

   if (!this->IsShown())
      return;

   for (unsigned int i = 0; i < mMixerTrackClusters.GetCount(); i++)
      mMixerTrackClusters[i]->ResetMeter(bResetClipping);
}

void MixerBoard::UpdateName(const PlayableTrack* pTrack)
{
   MixerTrackCluster* pMixerTrackCluster;
   this->FindMixerTrackCluster(pTrack, &pMixerTrackCluster);
   if (pMixerTrackCluster)
      pMixerTrackCluster->UpdateName();
}

void MixerBoard::UpdateMute(const PlayableTrack* pTrack /*= NULL*/) // NULL means update for all tracks.
{
   if (pTrack == NULL)
   {
      for (unsigned int i = 0; i < mMixerTrackClusters.GetCount(); i++)
         mMixerTrackClusters[i]->UpdateMute();
   }
   else
   {
      MixerTrackCluster* pMixerTrackCluster;
      FindMixerTrackCluster(pTrack, &pMixerTrackCluster);
      if (pMixerTrackCluster)
         pMixerTrackCluster->UpdateMute();
   }
}

void MixerBoard::UpdateSolo(const PlayableTrack* pTrack /*= NULL*/) // NULL means update for all tracks.
{
   if (pTrack == NULL)
   {
      for (unsigned int i = 0; i < mMixerTrackClusters.GetCount(); i++)
         mMixerTrackClusters[i]->UpdateSolo();
   }
   else
   {
      MixerTrackCluster* pMixerTrackCluster;
      FindMixerTrackCluster(pTrack, &pMixerTrackCluster);
      if (pMixerTrackCluster)
         pMixerTrackCluster->UpdateSolo();
   }
}

void MixerBoard::UpdatePan(const PlayableTrack* pTrack)
{
   if (pTrack == NULL)
   {
      for (unsigned int i = 0; i < mMixerTrackClusters.GetCount(); i++)
         mMixerTrackClusters[i]->UpdatePan();
   }
   else
   {
      MixerTrackCluster* pMixerTrackCluster;
      FindMixerTrackCluster(pTrack, &pMixerTrackCluster);
      if (pMixerTrackCluster)
         pMixerTrackCluster->UpdatePan();
   }
}

void MixerBoard::UpdateGain(const PlayableTrack* pTrack)
{
   MixerTrackCluster* pMixerTrackCluster;
   FindMixerTrackCluster(pTrack, &pMixerTrackCluster);
   if (pMixerTrackCluster)
      pMixerTrackCluster->UpdateGain();
}

#ifdef EXPERIMENTAL_MIDI_OUT
void MixerBoard::UpdateVelocity(const PlayableTrack* pTrack)
{
   MixerTrackCluster* pMixerTrackCluster;
   FindMixerTrackCluster(pTrack, &pMixerTrackCluster);
   if (pMixerTrackCluster)
      pMixerTrackCluster->UpdateVelocity();
}
#endif

void MixerBoard::UpdateMeters(const double t1, const bool bLoopedPlay)
{
   if (!this->IsShown() || (t1 == BAD_STREAM_TIME))
      return;

   if (mPrevT1 == BAD_STREAM_TIME)
   {
      mPrevT1 = t1;
      return;
   }

   // In loopedPlay mode, at the end of the loop, mPrevT1 is set to
   // selection end, so the next t1 will be less, but we do want to
   // keep updating the meters.
   if (t1 <= mPrevT1)
   {
      if (bLoopedPlay)
         mPrevT1 = t1;
      return;
   }

   for (unsigned int i = 0; i < mMixerTrackClusters.GetCount(); i++)
      mMixerTrackClusters[i]->UpdateMeter(mPrevT1, t1);

   mPrevT1 = t1;
}


void MixerBoard::UpdateWidth()
{
   int newWidth = this->GetTrackClustersWidth();

   // Min width is one cluster wide, plus margins.
   if (newWidth < MIXER_BOARD_MIN_WIDTH)
      newWidth = MIXER_BOARD_MIN_WIDTH;

   mScrolledWindow->SetVirtualSize(newWidth, -1);
   this->GetParent()->SetSize(newWidth + kDoubleInset, -1);
}

//
// private methods
//


void MixerBoard::MakeButtonBitmap( wxMemoryDC & dc, wxBitmap & WXUNUSED(bitmap), wxRect & bev, const wxString & str, bool up )
{

   int textWidth, textHeight;

   int fontSize = 10;
   #ifdef __WXMSW__
      fontSize = 8;
   #endif
   wxFont font(fontSize, wxSWISS, wxNORMAL, wxNORMAL);
   GetTextExtent(str, &textWidth, &textHeight, NULL, NULL, &font);

   AColor::UseThemeColour( &dc, clrMedium );
   dc.DrawRectangle(bev);

   AColor::Bevel2( dc, up, bev, false );

   wxCoord x = bev.x + (bev.width - textWidth) / 2;
   wxCoord y = bev.y + (bev.height - textHeight) / 2;
   dc.SetFont(font);
   dc.SetTextForeground(theTheme.Colour(clrTrackPanelText));
   dc.SetBackgroundMode(wxTRANSPARENT);
   dc.DrawText(str, x, y);
//   dc.DrawText(str, 0, 0);
}

void MixerBoard::CreateMuteSoloImages()
{
   // Much of this is similar to TrackInfo::MuteOrSoloDrawFunction.
   wxMemoryDC dc;
   wxString str = _("Mute");

   //mMuteSoloWidth = textWidth + kQuadrupleInset;
   //if (mMuteSoloWidth < kRightSideStackWidth - kInset)
   mMuteSoloWidth = kRightSideStackWidth - kInset;

   wxBitmap bitmap(mMuteSoloWidth, MUTE_SOLO_HEIGHT,24);
   dc.SelectObject(bitmap);
   wxRect bev(0, 0, mMuteSoloWidth, MUTE_SOLO_HEIGHT);
   
   const bool up=true;
   const bool down=false;

   MakeButtonBitmap( dc, bitmap, bev, str, up );
   mImageMuteUp = std::make_unique<wxImage>(bitmap.ConvertToImage());
   mImageMuteOver = std::make_unique<wxImage>(bitmap.ConvertToImage()); // Same as up, for now.

   MakeButtonBitmap( dc, bitmap, bev, str, down );
   //AColor::Bevel(dc, false, bev);
   mImageMuteDown = std::make_unique<wxImage>(bitmap.ConvertToImage());

   MakeButtonBitmap( dc, bitmap, bev, str, down );
   mImageMuteDownWhileSolo = std::make_unique<wxImage>(bitmap.ConvertToImage());

   mImageMuteDisabled = std::make_unique<wxImage>(mMuteSoloWidth, MUTE_SOLO_HEIGHT); // Leave empty because unused.

   str = _("Solo");
   MakeButtonBitmap( dc, bitmap, bev, str, up );
   mImageSoloUp = std::make_unique<wxImage>(bitmap.ConvertToImage());
   mImageSoloOver = std::make_unique<wxImage>(bitmap.ConvertToImage()); // Same as up, for now.

   MakeButtonBitmap( dc, bitmap, bev, str, down );
   mImageSoloDown = std::make_unique<wxImage>(bitmap.ConvertToImage());

   mImageSoloDisabled = std::make_unique<wxImage>(mMuteSoloWidth, MUTE_SOLO_HEIGHT); // Leave empty because unused.
}

int MixerBoard::FindMixerTrackCluster(const PlayableTrack* pTrack,
                                        MixerTrackCluster** hMixerTrackCluster) const
{
   *hMixerTrackCluster = NULL;
   for (unsigned int i = 0; i < mMixerTrackClusters.GetCount(); i++)
   {
      if (mMixerTrackClusters[i]->mTrack == pTrack)
      {
         *hMixerTrackCluster = mMixerTrackClusters[i];
         return i;
      }
   }
   return -1;
}

void MixerBoard::LoadMusicalInstruments()
{
   const struct Data { const char **bitmap; wxString name; } table[] = {
      {acoustic_guitar_gtr_xpm, wxT("acoustic_guitar_gtr")},
      {acoustic_piano_pno_xpm, wxT("acoustic_piano_pno")},
      {back_vocal_bg_vox_xpm, wxT("back_vocal_bg_vox")},
      {clap_xpm, wxT("clap")},
      {drums_dr_xpm, wxT("drums_dr")},
      {electric_bass_guitar_bs_gtr_xpm, wxT("electric_bass_guitar_bs_gtr")},
      {electric_guitar_gtr_xpm, wxT("electric_guitar_gtr")},
      {electric_piano_pno_key_xpm, wxT("electric_piano_pno_key")},
      {kick_xpm, wxT("kick")},
      {loop_xpm, wxT("loop")},
      {organ_org_xpm, wxT("organ_org")},
      {perc_xpm, wxT("perc")},
      {sax_xpm, wxT("sax")},
      {snare_xpm, wxT("snare")},
      {string_violin_cello_xpm, wxT("string_violin_cello")},
      {synth_xpm, wxT("synth")},
      {tambo_xpm, wxT("tambo")},
      {trumpet_horn_xpm, wxT("trumpet_horn")},
      {turntable_xpm, wxT("turntable")},
      {vibraphone_vibes_xpm, wxT("vibraphone_vibes")},
      {vocal_vox_xpm, wxT("vocal_vox")},

      // This one must be last, so it wins when best score is 0.
      {_default_instrument_xpm, wxEmptyString},
   };

   wxRect bev(1, 1, MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH - 2, MUSICAL_INSTRUMENT_HEIGHT_AND_WIDTH - 2);
   wxMemoryDC dc;

   for (const auto &data : table) {
      auto bmp = std::make_unique<wxBitmap>(data.bitmap);
      dc.SelectObject(*bmp);
      AColor::Bevel(dc, false, bev);
      mMusicalInstruments.push_back(make_movable<MusicalInstrument>(
         std::move(bmp), data.name
      ));
   };
}

// event handlers

void MixerBoard::OnSize(wxSizeEvent &evt)
{
   // this->FitInside() doesn't work, and it doesn't happen automatically. Is wxScrolledWindow wrong?
   mScrolledWindow->SetSize(evt.GetSize());

   this->ResizeTrackClusters();
   this->RefreshTrackClusters(true);
}

void MixerBoard::OnTimer(wxCommandEvent &event)
{
   // PRL 12 Jul 2015:  Moved the below (with comments) out of TrackPanel::OnTimer.

   // Vaughan, 2011-01-28: No longer doing this on timer.
   //   Now it's in AudioIO::SetMeters() and AudioIO::StopStream(), as with Meter Toolbar meters.
   //if (pMixerBoard)
   //   pMixerBoard->ResetMeters(false);

   //v Vaughan, 2011-02-25: Moved this update back here from audacityAudioCallback.
   //    See note there.
   // Vaughan, 2010-01-30:
   //    Since all we're doing here is updating the meters, I moved it to
   //    audacityAudioCallback where it calls gAudioIO->mOutputMeter->UpdateDisplay().
   if (mProject->IsAudioActive())
   {
      UpdateMeters(gAudioIO->GetStreamTime(),
                   (mProject->mLastPlayMode == PlayMode::loopedPlay));
   }

   // Let other listeners get the notification
   event.Skip();
}


// class MixerBoardFrame

BEGIN_EVENT_TABLE(MixerBoardFrame, wxFrame)
   EVT_KEY_DOWN(MixerBoardFrame::OnKeyEvent)
   EVT_CLOSE(MixerBoardFrame::OnCloseWindow)
   EVT_MAXIMIZE(MixerBoardFrame::OnMaximize)
   EVT_SIZE(MixerBoardFrame::OnSize)
END_EVENT_TABLE()

// Default to fitting one track.
const wxSize kDefaultSize =
   wxSize(MIXER_BOARD_MIN_WIDTH, MIXER_BOARD_MIN_HEIGHT);

MixerBoardFrame::MixerBoardFrame(AudacityProject* parent)
: wxFrame(parent, -1,
          wxString::Format(_("Audacity Mixer Board%s"),
                           ((parent->GetName() == wxEmptyString) ?
                              wxT("") :
                              wxString::Format(wxT(" - %s"),
                                             parent->GetName()))),
            wxDefaultPosition, kDefaultSize,
            //vvv Bug in wxFRAME_FLOAT_ON_PARENT:
            // If both the project frame and MixerBoardFrame are minimized and you restore MixerBoardFrame,
            // you can't restore project frame until you close MixerBoardFrame, but then project frame and
            // MixerBoardFrame are restored but MixerBoardFrame is unresponsive because it thinks it's not shown.
            //    wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT)
            wxDEFAULT_FRAME_STYLE)
{
   mMixerBoard = safenew MixerBoard(parent, this, wxDefaultPosition, kDefaultSize);

   this->SetSizeHints(MIXER_BOARD_MIN_WIDTH, MIXER_BOARD_MIN_HEIGHT);

   mMixerBoard->UpdateTrackClusters();

   // loads either the XPM or the windows resource, depending on the platform
#if !defined(__WXMAC__) && !defined(__WXX11__)
   {
#ifdef __WXMSW__
      wxIcon ic{ wxICON(AudacityLogo) };
#else
      wxIcon ic{wxICON(AudacityLogo48x48)};
#endif
      SetIcon(ic);
   }
#endif
   Center();
}

MixerBoardFrame::~MixerBoardFrame()
{
}


// event handlers
void MixerBoardFrame::OnCloseWindow(wxCloseEvent &WXUNUSED(event))
{
   this->Hide();
}

void MixerBoardFrame::OnMaximize(wxMaximizeEvent &event)
{
   // Update the size hints to show all tracks before skipping to let default handling happen.
   mMixerBoard->UpdateWidth();
   event.Skip();
}

void MixerBoardFrame::OnSize(wxSizeEvent & WXUNUSED(event))
{
   mMixerBoard->SetSize(this->GetClientSize());
}

void MixerBoardFrame::OnKeyEvent(wxKeyEvent & event)
{
   AudacityProject *project = GetActiveProject();
   project->GetCommandManager()->FilterKeyEvent(project, event, true);
}


