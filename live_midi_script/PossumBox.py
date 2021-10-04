from __future__ import absolute_import, print_function, unicode_literals
from _Framework.ControlSurface import ControlSurface
from _Framework.InputControlElement import *
from _Framework.MixerComponent import MixerComponent
from _Framework.SliderElement import SliderElement
from _Framework.ButtonElement import ButtonElement
from _Framework.SessionComponent import SessionComponent
from _Framework.SceneComponent import SceneComponent
from _Framework.ClipSlotComponent import ClipSlotComponent
from _Framework.TransportComponent import TransportComponent

NUM_TRACKS = 8

MIDI_CHANNEL = 8 - 1 # Live adds 1 to this for some reason
TRACK_COUNT_CC = 126
BUTTON_CC_BASE = 50
POT_CC_BASE =  20
MASTER_PLAY_CC = 110
MASTER_REC_CC = 111
PLAY_BTN_OFFSET = 16
MASTER_POT_1_CC = 115
MASTER_POT_2_CC = 116
MASTER_POT_3_CC = 117
MASTER_POT_4_CC = 118


sendMidi = [None]

lastPlayedClipByTrack = {}

def getCcBase(trackIndex):
    return BUTTON_CC_BASE + (15 - (trackIndex * 2))

def sendCc(cc, value):
    # See InputControlElement._do_send_value()
    status_byte = MIDI_CHANNEL + MIDI_CC_STATUS
    sendMidi[0]((status_byte, cc, value))


class _ClipSlotComponent(ClipSlotComponent):
    def __init__(self, *a, **kw):
        ClipSlotComponent.__init__(self, *a, **kw)
        self._wasPlaying = False
        self._parentScene = None
        self._trackIndex = None

    def update(self):
        ClipSlotComponent.update(self)
        sceneIndex = getattr(self._parentScene, '_sceneIndex', 0)
        if sceneIndex <= 0:
            # sceneIndex 0 seems to be something... weird... like a global state for the scene?
            return

        if not self._clip_slot:
            self._wasPlaying = False
            return

        if self._clip_slot.is_playing != self._wasPlaying:
            if self._clip_slot.is_playing:
                lastPlayedClipByTrack[self._trackIndex] = self._clip_slot
            # eg track 0 -> cc 81 (P1)
            cc = getCcBase(self._trackIndex) + PLAY_BTN_OFFSET
            sendCc(cc, 127 if self._clip_slot.is_playing else 0)
        
        self._wasPlaying = self._clip_slot.is_playing



class _SceneComponent(SceneComponent):
    clip_slot_component_type = _ClipSlotComponent

    def __init__(self, *a, **kw):
        SceneComponent.__init__(self, *a, **kw)
        self._sceneIndex = None

    def _create_clip_slot(self):
        cs = SceneComponent._create_clip_slot(self)
        cs._parentScene = self
        cs._trackIndex = len(self._clip_slots)
        return cs


class _SessionComponent(SessionComponent):
    scene_component_type = _SceneComponent

    def __init__(self, *a, **kw):
        self._sceneCount = 0
        SessionComponent.__init__(self, *a, **kw)

    def _create_scene(self):
        sc = SessionComponent._create_scene(self)
        sc._sceneIndex = self._sceneCount
        self._sceneCount += 1
        return sc


class StopPlayButtonElement(ButtonElement):
    def __init__(self, session, track, cc):
        ButtonElement.__init__(self, True, MIDI_CC_TYPE, MIDI_CHANNEL, cc)
        self._session = session
        self._track = track

    def receive_value(self, value):
        # NOTE: This does not change the LED state
        # Idea: blink LED while clip is waiting to start
        tracks = self._session.tracks_to_use()
        if self._track >= len(tracks):
            return
        
        track = tracks[self._track]
        isPlaying = track.playing_slot_index >= 0
        if isPlaying:
            track.stop_all_clips()
        else:
            # Track was stopped, so we want to trigger a clip
            # ...either the last one that was playing, or the first
            clip = lastPlayedClipByTrack.get(self._track, next(iter(track.clip_slots), None))
            if clip:
                # Note: not sure clip_slots can actually be empty
                clip.fire()


from _Framework.ControlSurfaceComponent import ControlSurfaceComponent
from _Framework.SubjectSlot import subject_slot

class _ToggleComponent(ControlSurfaceComponent):
    is_private = True

    def __init__(self, song, *a, **k):
        super(_ToggleComponent, self).__init__(*a, **k)
        self._property_name = 'is_playing'
        self._property_slot = self.register_slot(song, self._update_button, 'is_playing')

    def _get_subject(self):
        return self._property_slot.subject

    def _set_subject(self, model):
        self._property_slot.subject = model
        self.update()

    subject = property(_get_subject, _set_subject)

    def _get_value(self):
        if self.subject:
            return getattr(self.subject, 'is_playing')

    def _set_value(self, value):
        setattr(self.subject, 'is_playing', value)

    value = property(_get_value, _set_value)

    def set_toggle_button(self, button):
        self._on_button_value.subject = button
        self._update_button()

    def update(self):
        super(_ToggleComponent, self).update()
        self._update_button()

    def _update_button(self):
        button = self._on_button_value.subject
        if button:
            button.set_light(self.value)

    @subject_slot(u'value')
    def _on_button_value(self, value):
        # HERE BE THE MAGIC
        # Not actually sure why I overrode all those methods. Maybe just need this and _update_button?
        self.value = not bool(self.value)



class PossumBox(ControlSurface):
    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        with self.component_guard():
            sendMidi[0] = self._send_midi
            
            self._suggested_input_port = 'Teensy MIDI'
            self._suggested_output_port = 'Teensy MIDI'


            _ToggleComponent(self.song()).set_toggle_button(ButtonElement(True, MIDI_CC_TYPE, MIDI_CHANNEL, MASTER_PLAY_CC))
            
            TransportComponent().set_record_button(ButtonElement(True, MIDI_CC_TYPE, MIDI_CHANNEL, MASTER_REC_CC))

            session = _SessionComponent(num_tracks = NUM_TRACKS, num_scenes = 100)
            startStopButtons = []

            mixer = MixerComponent(NUM_TRACKS)
            for track in range(NUM_TRACKS):
                strip = mixer.channel_strip(track)

                ccBase = BUTTON_CC_BASE + (15 - (track * 2))

                strip.set_mute_button(ButtonElement(True, MIDI_CC_TYPE, MIDI_CHANNEL, ccBase))
                strip.set_invert_mute_feedback(True)
                strip.set_solo_button(ButtonElement(True, MIDI_CC_TYPE, MIDI_CHANNEL, ccBase - 1))
                strip.set_arm_button(ButtonElement(True, MIDI_CC_TYPE, MIDI_CHANNEL, ccBase + 15))
                
                startStopButtons.append(StopPlayButtonElement(session, track, ccBase + PLAY_BTN_OFFSET))

                strip.set_volume_control(SliderElement(MIDI_CC_TYPE, MIDI_CHANNEL, POT_CC_BASE + track))

                # U[n] default mapping, for now
                # None for A (default delay), use it for B (reverb) instead
                strip.set_send_controls([ None, SliderElement(MIDI_CC_TYPE, MIDI_CHANNEL, POT_CC_BASE + 8 + track) ])

            # Note: No (default) mappings for the 4 master pots
                
            session.set_stop_track_clip_buttons(startStopButtons)


    def _on_track_list_changed(self):
        sendCc(TRACK_COUNT_CC, len(self.song().visible_tracks))
        return ControlSurface._on_track_list_changed(self)
