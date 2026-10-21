add_library(eg13_core STATIC
  engine/plaits_engine.cpp
  engine/braids_engine.cpp
  engine/elements_engine.cpp
  engine/rings_engine.cpp
  engine/audio_input_engine.cpp
  engine/engine_registry.cpp
  engine/bypass_engine.cpp
  engine/native_rate_wrapper.cpp
  engine/ripples_engine.cpp
  engine/shelves_engine.cpp
  engine/rack_shim/rack_random.cpp
  engine/sst_filter_engine.cpp
  engine/svf_engine.cpp
  engine/warps_engine.cpp
  engine/mod_engine.cpp
  synth/param_table.cpp
  synth/synth.cpp
  state/state.cpp
  voice/voice_allocator.cpp
  voice/voice.cpp
  voice/source_factory.cpp
  voice/processor_factory.cpp
  voice/processor_slot.cpp
  part/part.cpp)
target_include_directories(eg13_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eg13_core PRIVATE eg13_eurorack eg13_nlohmann eg13_r8brain eg13_vcv eg13_sst)
target_compile_options(eg13_core PRIVATE -Wall -Wextra -Werror)
# The VCV models are built as Rack builds its plugins (VCVRack/Rack compile.mk:17:
# "FLAGS += -O3 -funsafe-math-optimizations -fno-omit-frame-pointer"), -O3 included in every
# configuration: about 30 % faster, measured during planning. Owner decision 2026-10-20. Only the
# models get the flags, each in its own TU inside the inline namespace eg13::rack_flags, following
# the isolation rules at the top of engine/ripples_model.cpp; their adapters are built like every
# other engine. The ctest "flagged VCV models define symbols
# only in their rack_flags namespace" (scripts/check-flagged-symbols.sh) checks these objects.
add_library(eg13_vcv_flagged OBJECT engine/ripples_model.cpp engine/shelves_model.cpp)
target_include_directories(eg13_vcv_flagged PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eg13_vcv_flagged PRIVATE eg13_vcv)
target_compile_options(eg13_vcv_flagged PRIVATE -Wall -Wextra -Werror -O3 -funsafe-math-optimizations)
target_sources(eg13_core PRIVATE $<TARGET_OBJECTS:eg13_vcv_flagged>)
