/**************************************************************************/
/*  audio_stream_ogg_opus.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "audio_stream_ogg_opus.h"

#include "core/io/file_access.h"
#include "core/variant/typed_array.h"

#include "modules/opus/resource_importer_ogg_opus.h"
#include <ogg/ogg.h>

int AudioStreamPlaybackOggVorbis::_mix_internal(AudioFrame *p_buffer, int p_frames) {
	ERR_FAIL_COND_V(!ready, 0);

	if (!active) {
		return 0;
	}

	int todo = p_frames;

	int beat_length_frames = -1;
	bool use_loop = looping_override ? looping : vorbis_stream->loop;

	if (use_loop && vorbis_stream->get_bpm() > 0 && vorbis_stream->get_beat_count() > 0) {
		beat_length_frames = vorbis_stream->get_beat_count() * vorbis_data->get_sampling_rate() * 60 / vorbis_stream->get_bpm();
	}

	while (todo > 0 && active) {
		AudioFrame *buffer = p_buffer;
		buffer += p_frames - todo;

		int to_mix = todo;
		if (beat_length_frames >= 0 && (beat_length_frames - (int)frames_mixed) < to_mix) {
			to_mix = MAX(0, beat_length_frames - (int)frames_mixed);
		}

		int mixed = _mix_frames_vorbis(buffer, to_mix);
		ERR_FAIL_COND_V(mixed < 0, 0);
		todo -= mixed;
		frames_mixed += mixed;

		if (loop_fade_remaining < FADE_SIZE) {
			int to_fade = loop_fade_remaining + MIN(FADE_SIZE - loop_fade_remaining, mixed);
			for (int i = loop_fade_remaining; i < to_fade; i++) {
				buffer[i - loop_fade_remaining] += loop_fade[i] * (float(FADE_SIZE - i) / float(FADE_SIZE));
			}
			loop_fade_remaining = to_fade;
		}

		if (beat_length_frames >= 0) {
			/**
			 * Length determined by beat length
			 * This code is commented out because, in practice, it is preferred that the fade
			 * is done by the transitioner and this stream just goes on until it ends while fading out.
			 *
			 * End fade implementation is left here for reference in case at some point this feature
			 * is desired.

			if (!beat_loop && (int)frames_mixed > beat_length_frames - FADE_SIZE) {
				print_line("beat length fade/after mix?");
				//No loop, just fade and finish
				for (int i = 0; i < mixed; i++) {
					int idx = frames_mixed + i - mixed;
					buffer[i] *= 1.0 - float(MAX(0, (idx - (beat_length_frames - FADE_SIZE)))) / float(FADE_SIZE);
				}
				if ((int)frames_mixed == beat_length_frames) {
					for (int i = p_frames - todo; i < p_frames; i++) {
						p_buffer[i] = AudioFrame(0, 0);
					}
					active = false;
					break;
				}
			} else
			**/

			if (use_loop && beat_length_frames <= (int)frames_mixed) {
				// End of file when doing beat-based looping. <= used instead of == because importer editing
				if (!have_packets_left && !have_samples_left) {
					//Nothing remaining, so do nothing.
					loop_fade_remaining = FADE_SIZE;
				} else {
					// Add some loop fade;
					int faded_mix = _mix_frames_vorbis(loop_fade, FADE_SIZE);

					for (int i = faded_mix; i < FADE_SIZE; i++) {
						// In case lesss was mixed, pad with zeros
						loop_fade[i] = AudioFrame(0, 0);
					}
					loop_fade_remaining = 0;
				}

				seek(vorbis_stream->loop_offset);
				loops++;
				// We still have buffer to fill, start from this element in the next iteration.
				continue;
			}
		}

		if (!have_packets_left && !have_samples_left) {
			// Actual end of file!
			bool is_not_empty = mixed > 0 || vorbis_stream->get_length() > 0;
			if (use_loop && is_not_empty) {
				//loop

				seek(vorbis_stream->loop_offset);
				loops++;
				// We still have buffer to fill, start from this element in the next iteration.

			} else {
				for (int i = p_frames - todo; i < p_frames; i++) {
					p_buffer[i] = AudioFrame(0, 0);
				}
				active = false;
			}
		}
	}
	return p_frames - todo;
}

int AudioStreamPlaybackOggVorbis::_mix_frames_vorbis(AudioFrame *p_buffer, int p_frames) {
}

float AudioStreamPlaybackOggVorbis::get_stream_sampling_rate() {
	return vorbis_data->get_sampling_rate();
}

bool AudioStreamPlaybackOggVorbis::_alloc_vorbis() {
}

void AudioStreamPlaybackOggVorbis::start(double p_from_pos) {
	ERR_FAIL_COND(!ready);
	loop_fade_remaining = FADE_SIZE;
	active = true;
	seek(p_from_pos);
	loops = 0;
	begin_resample();
}

void AudioStreamPlaybackOggVorbis::stop() {
	active = false;
}

bool AudioStreamPlaybackOggVorbis::is_playing() const {
	return active;
}

int AudioStreamPlaybackOggVorbis::get_loop_count() const {
	return loops;
}

double AudioStreamPlaybackOggVorbis::get_playback_position() const {
	return double(frames_mixed) / (double)vorbis_data->get_sampling_rate();
}

void AudioStreamPlaybackOggVorbis::tag_used_streams() {
	vorbis_stream->tag_used(get_playback_position());
}

void AudioStreamPlaybackOggVorbis::set_parameter(const StringName &p_name, const Variant &p_value) {
	if (p_name == SNAME("looping")) {
		if (p_value == Variant()) {
			looping_override = false;
			looping = false;
		} else {
			looping_override = true;
			looping = p_value;
		}
	}
}

Variant AudioStreamPlaybackOggVorbis::get_parameter(const StringName &p_name) const {
	if (looping_override && p_name == SNAME("looping")) {
		return looping;
	}
	return Variant();
}

void AudioStreamPlaybackOggVorbis::seek(double p_time) {
}

void AudioStreamPlaybackOggVorbis::set_is_sample(bool p_is_sample) {
	_is_sample = p_is_sample;
}

bool AudioStreamPlaybackOggVorbis::get_is_sample() const {
	return _is_sample;
}

Ref<AudioSamplePlayback> AudioStreamPlaybackOggVorbis::get_sample_playback() const {
	return sample_playback;
}

void AudioStreamPlaybackOggVorbis::set_sample_playback(const Ref<AudioSamplePlayback> &p_playback) {
	sample_playback = p_playback;
	if (sample_playback.is_valid()) {
		sample_playback->stream_playback = Ref<AudioStreamPlayback>(this);
	}
}

AudioStreamPlaybackOggVorbis::~AudioStreamPlaybackOggVorbis() {
}

Ref<AudioStreamPlayback> AudioStreamOggVorbis::instantiate_playback() {
	Ref<AudioStreamPlaybackOggVorbis> ovs;

	ERR_FAIL_COND_V(packet_sequence.is_null(), nullptr);

	ovs.instantiate();
	ovs->vorbis_stream = Ref<AudioStreamOggVorbis>(this);
	ovs->vorbis_data = packet_sequence;
	ovs->frames_mixed = 0;
	ovs->active = false;
	ovs->loops = 0;
	if (ovs->_alloc_vorbis()) {
		return ovs;
	}
	// Failed to allocate data structures.
	return nullptr;
}

String AudioStreamOggVorbis::get_stream_name() const {
	return ""; //return stream_name;
}

void AudioStreamOggVorbis::maybe_update_info() {
}

void AudioStreamOggVorbis::set_packet_sequence(Ref<OggPacketSequence> p_packet_sequence) {
	packet_sequence = p_packet_sequence;
	if (packet_sequence.is_valid()) {
		maybe_update_info();
	}
}

Ref<OggPacketSequence> AudioStreamOggVorbis::get_packet_sequence() const {
	return packet_sequence;
}

void AudioStreamOggVorbis::set_loop(bool p_enable) {
	loop = p_enable;
}

bool AudioStreamOggVorbis::has_loop() const {
	return loop;
}

void AudioStreamOggVorbis::set_loop_offset(double p_seconds) {
	loop_offset = p_seconds;
}

double AudioStreamOggVorbis::get_loop_offset() const {
	return loop_offset;
}

double AudioStreamOggVorbis::get_length() const {
	ERR_FAIL_COND_V(packet_sequence.is_null(), 0);
	return packet_sequence->get_length();
}

void AudioStreamOggVorbis::set_bpm(double p_bpm) {
	ERR_FAIL_COND(p_bpm < 0);
	bpm = p_bpm;
	emit_changed();
}

double AudioStreamOggVorbis::get_bpm() const {
	return bpm;
}

void AudioStreamOggVorbis::set_beat_count(int p_beat_count) {
	ERR_FAIL_COND(p_beat_count < 0);
	beat_count = p_beat_count;
	emit_changed();
}

int AudioStreamOggVorbis::get_beat_count() const {
	return beat_count;
}

void AudioStreamOggVorbis::set_bar_beats(int p_bar_beats) {
	ERR_FAIL_COND(p_bar_beats < 2);
	bar_beats = p_bar_beats;
	emit_changed();
}

int AudioStreamOggVorbis::get_bar_beats() const {
	return bar_beats;
}

bool AudioStreamOggVorbis::is_monophonic() const {
	return false;
}

void AudioStreamOggVorbis::get_parameter_list(List<Parameter> *r_parameters) {
	r_parameters->push_back(Parameter(PropertyInfo(Variant::BOOL, "looping", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CHECKABLE), Variant()));
}

Ref<AudioSample> AudioStreamOggVorbis::generate_sample() const {
	Ref<AudioSample> sample;
	sample.instantiate();
	sample->stream = this;
	sample->loop_mode = loop
			? AudioSample::LoopMode::LOOP_FORWARD
			: AudioSample::LoopMode::LOOP_DISABLED;
	sample->loop_begin = loop_offset;
	sample->loop_end = 0;
	return sample;
}

void AudioStreamOggVorbis::_bind_methods() {
	ClassDB::bind_static_method("AudioStreamOggVorbis", D_METHOD("load_from_buffer", "buffer"), &AudioStreamOggVorbis::load_from_buffer);
	ClassDB::bind_static_method("AudioStreamOggVorbis", D_METHOD("load_from_file", "path"), &AudioStreamOggVorbis::load_from_file);

	ClassDB::bind_method(D_METHOD("set_packet_sequence", "packet_sequence"), &AudioStreamOggVorbis::set_packet_sequence);
	ClassDB::bind_method(D_METHOD("get_packet_sequence"), &AudioStreamOggVorbis::get_packet_sequence);

	ClassDB::bind_method(D_METHOD("set_loop", "enable"), &AudioStreamOggVorbis::set_loop);
	ClassDB::bind_method(D_METHOD("has_loop"), &AudioStreamOggVorbis::has_loop);

	ClassDB::bind_method(D_METHOD("set_loop_offset", "seconds"), &AudioStreamOggVorbis::set_loop_offset);
	ClassDB::bind_method(D_METHOD("get_loop_offset"), &AudioStreamOggVorbis::get_loop_offset);

	ClassDB::bind_method(D_METHOD("set_bpm", "bpm"), &AudioStreamOggVorbis::set_bpm);
	ClassDB::bind_method(D_METHOD("get_bpm"), &AudioStreamOggVorbis::get_bpm);

	ClassDB::bind_method(D_METHOD("set_beat_count", "count"), &AudioStreamOggVorbis::set_beat_count);
	ClassDB::bind_method(D_METHOD("get_beat_count"), &AudioStreamOggVorbis::get_beat_count);

	ClassDB::bind_method(D_METHOD("set_bar_beats", "count"), &AudioStreamOggVorbis::set_bar_beats);
	ClassDB::bind_method(D_METHOD("get_bar_beats"), &AudioStreamOggVorbis::get_bar_beats);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "packet_sequence", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_packet_sequence", "get_packet_sequence");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bpm", PROPERTY_HINT_RANGE, "0,400,0.01,or_greater"), "set_bpm", "get_bpm");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "beat_count", PROPERTY_HINT_RANGE, "0,512,1,or_greater"), "set_beat_count", "get_beat_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bar_beats", PROPERTY_HINT_RANGE, "2,32,1,or_greater"), "set_bar_beats", "get_bar_beats");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "has_loop");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "loop_offset"), "set_loop_offset", "get_loop_offset");
}

AudioStreamOggVorbis::AudioStreamOggVorbis() {}

AudioStreamOggVorbis::~AudioStreamOggVorbis() {}

Ref<AudioStreamOggVorbis> AudioStreamOggVorbis::load_from_buffer(const Vector<uint8_t> &file_data) {
	return ResourceImporterOggVorbis::load_from_buffer(file_data);
}

Ref<AudioStreamOggVorbis> AudioStreamOggVorbis::load_from_file(const String &p_path) {
	return ResourceImporterOggVorbis::load_from_file(p_path);
}
