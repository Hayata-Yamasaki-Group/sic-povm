// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/guarded_writer.hpp"

#include "construct/model/session_state.hpp"

namespace sic::construct {
std::unique_ptr<sic::sicx::ProducerWriter> open_guarded_writer(
    SicxWriter& component, QuarantineOutput& quarantine, const sic::sicx::Meta& header,
    const sic::sicx::StreamOptions& options, unsigned threads, const std::string& record,
    const sic::construct::ProductionPolicy& policy) {
  const bool completed = quarantine.has_completed_replay();
  if (completed) {
    for (const auto* suffix : {".spool.partial", ".spool.partial.journal"}) {
      struct stat artifact {};
      const auto path = quarantine.path().string() + suffix;
      if (::lstat(path.c_str(), &artifact) || !S_ISREG(artifact.st_mode))
        throw sic::Error(sic::ErrorKind::integrity,
                         "completed resume requires its durable spool and journal");
    }
  }
  auto output = component.open(quarantine.path(), header, options, threads, policy);
  output->preserve_spool_for_resume();
  const std::string key = "producer components";
  if (const auto* previous = output->writer().note(key)) {
    if (std::string(previous->begin(), previous->end()) != record)
      throw sic::Error(sic::ErrorKind::integrity, "resume: producer component choice differs");
  } else {
    if (completed || output->writer().pending_replay())
      throw sic::Error(sic::ErrorKind::integrity, "resume: missing producer component note");
    output->writer().put_note(key, sic::sicx::ByteBuffer(record.begin(), record.end()));
  }
  quarantine.prepare_replay(true);
  return output;
}

}  // namespace sic::construct

namespace sic::construct {
std::unique_ptr<sic::sicx::ProducerWriter> Construction::open_writer(
    const sic::sicx::Meta& header, const sic::sicx::StreamOptions& options) {
  const std::string record = component_record(components, component_checkpoints);
  return open_guarded_writer(*components.writer, *quarantine, header, options, (unsigned)nth,
                             record, producer_options.policy);
}
}  // namespace sic::construct
