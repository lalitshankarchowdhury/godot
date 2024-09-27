/**************************************************************************/
/*  resource_importer_ogg_opus.cpp                                      */
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

#include "resource_importer_ogg_opus.h"

#include "core/io/file_access.h"
#include "core/io/resource_saver.h"
#include "scene/resources/texture.h"

#ifdef TOOLS_ENABLED
#include "editor/import/audio_stream_import_settings.h"
#endif

#include <ogg/ogg.h>
#include <opus/opus.h>

String ResourceImporterOggOpus::get_importer_name() const {
	return "oggopusstr";
}

String ResourceImporterOggOpus::get_visible_name() const {
	return "oggopusstr";
}

void ResourceImporterOggOpus::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("opus");
}

String ResourceImporterOggOpus::get_save_extension() const {
	return "oggopusstr";
}

String ResourceImporterOggOpus::get_resource_type() const {
	return "AudioStreamOggOpus";
}

bool ResourceImporterOggOpus::get_option_visibility(const String &p_path, const String &p_option, const HashMap<StringName, Variant> &p_options) const {
	return true;
}

int ResourceImporterOggOpus::get_preset_count() const {
	return 0;
}

String ResourceImporterOggOpus::get_preset_name(int p_idx) const {
	return String();
}

void ResourceImporterOggOpus::get_import_options(const String &p_path, List<ImportOption> *r_options, int p_preset) const {
	r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "loop"), false));
	r_options->push_back(ImportOption(PropertyInfo(Variant::FLOAT, "loop_offset"), 0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::FLOAT, "bpm", PROPERTY_HINT_RANGE, "0,400,0.01,or_greater"), 0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "beat_count", PROPERTY_HINT_RANGE, "0,512,or_greater"), 0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "bar_beats", PROPERTY_HINT_RANGE, "2,32,or_greater"), 4));
}

#ifdef TOOLS_ENABLED

bool ResourceImporterOggOpus::has_advanced_options() const {
	return true;
}

void ResourceImporterOggOpus::show_advanced_options(const String &p_path) {
	Ref<AudioStreamOggOpus> ogg_stream = load_from_file(p_path);
	if (ogg_stream.is_valid()) {
		AudioStreamImportSettingsDialog::get_singleton()->edit(p_path, "oggopusstr", ogg_stream);
	}
}
#endif

Error ResourceImporterOggOpus::import(const String &p_source_file, const String &p_save_path, const HashMap<StringName, Variant> &p_options, List<String> *r_platform_variants, List<String> *r_gen_files, Variant *r_metadata) {
	bool loop = p_options["loop"];
	double loop_offset = p_options["loop_offset"];
	double bpm = p_options["bpm"];
	int beat_count = p_options["beat_count"];
	int bar_beats = p_options["bar_beats"];

	Ref<AudioStreamOggOpus> ogg_opus_stream = load_from_file(p_source_file);
	if (ogg_opus_stream.is_null()) {
		return ERR_CANT_OPEN;
	}

	ogg_opus_stream->set_loop(loop);
	ogg_opus_stream->set_loop_offset(loop_offset);
	ogg_opus_stream->set_bpm(bpm);
	ogg_opus_stream->set_beat_count(beat_count);
	ogg_opus_stream->set_bar_beats(bar_beats);

	return ResourceSaver::save(ogg_opus_stream, p_save_path + ".oggopusstr");
}

ResourceImporterOggOpus::ResourceImporterOggOpus() {
}

void ResourceImporterOggOpus::_bind_methods() {
	ClassDB::bind_static_method("ResourceImporterOggOpus", D_METHOD("load_from_buffer", "buffer"), &ResourceImporterOggOpus::load_from_buffer);
	ClassDB::bind_static_method("ResourceImporterOggOpus", D_METHOD("load_from_file", "path"), &ResourceImporterOggOpus::load_from_file);
}

Ref<AudioStreamOggOpus> ResourceImporterOggOpus::load_from_buffer(const Vector<uint8_t> &file_data) {
	Ref<AudioStreamOggOpus> ogg_opus_stream;
	ogg_opus_stream.instantiate();

	ogg_sync_state sync_state;
	ogg_page page;
	int err;
	bool done = false;
	size_t cursor = 0;

	ogg_sync_init(&sync_state);

	while (!done) {
		if (cursor >= size_t(file_data.size())) {
			break;
		}

		err = ogg_sync_check(&sync_state);
		ERR_FAIL_COND_V_MSG(err != 0, Ref<AudioStreamOggOpus>(), "Ogg sync error " + itos(err));

		char *sync_buf = ogg_sync_buffer(&sync_state, OGG_SYNC_BUFFER_SIZE);
		err = ogg_sync_check(&sync_state);
		ERR_FAIL_COND_V_MSG(err != 0, Ref<AudioStreamOggOpus>(), "Ogg sync error " + itos(err));

		size_t copy_size = file_data.size() - cursor;
		if (copy_size > OGG_SYNC_BUFFER_SIZE) {
			copy_size = OGG_SYNC_BUFFER_SIZE;
		}

		memcpy(sync_buf, &file_data[cursor], copy_size);
		ogg_sync_wrote(&sync_state, copy_size);
		cursor += copy_size;
		err = ogg_sync_check(&sync_state);
		ERR_FAIL_COND_V_MSG(err != 0, Ref<AudioStreamOggOpus>(), "Ogg sync error " + itos(err));

		done = true;
	}

	ogg_sync_clear(&sync_state);

	return ogg_opus_stream;
}

Ref<AudioStreamOggOpus> ResourceImporterOggOpus::load_from_file(const String &p_path) {
	Vector<uint8_t> file_data = FileAccess::get_file_as_bytes(p_path);
	ERR_FAIL_COND_V_MSG(file_data.is_empty(), Ref<AudioStreamOggOpus>(), "Cannot open file '" + p_path + "'.");
	return load_from_buffer(file_data);
}
