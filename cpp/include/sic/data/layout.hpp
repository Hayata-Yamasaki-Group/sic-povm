// SPDX-License-Identifier: AGPL-3.0-or-later
// Release data tree (docs/sicx.md section 10a). The tree holds
// numerical data and manifests only; every SICX is a GitHub Release asset, transported as parts
// (docs/sicx.md section 10a):
//   ROOT/d<d>/{all.npz, stabilizers.txt}                        (other writers)
//   ROOT/d<d>/m<N>/manifest.json                                      (sic-multiplet-manifest/1.0)
//   ROOT/d<d>/m<N>/multiplet.sicx.parts.json                          (sicx-transport-parts/1.0)
//   ROOT/d<d>/m<N>/c<N>.json                                          (sic-fiducial-json/1.0, one
//   per ray) ROOT/d<d>/m<N>/c<N>.npy/.mat/.h5/.csv                             (other writers)
//   ROOT/catalogue.json                                               (sic-release-catalogue/1.0,
//   rebuilt from the tree)
// Construction conductors are excluded from public names, ids and files. The number N of m<N>
// follows increasing construction conductor within a dimension; it is given on the command line
// from provenance and is never derived from the SICX.
#pragma once

#include <fstream>
#include <iostream>
#include <set>

#include "sic/sicx/json.hpp"
#include "sic/transport/parts.hpp"

namespace sic::data {

void write_exclusive(const std::filesystem::path& path, const std::string& text);

struct ReleaseOptions {
  uint64_t multiplet = 0;            // N of m<N> (from provenance)
  std::filesystem::path assets_dir;  // optional: where the transport parts are written
  std::string repository = "OWNER/REPOSITORY", tag = "v1.0.0";
  uint64_t part_size = transport::kPartSize;
  unsigned threads = 1;
};

// N = rank of `conductor` among the construction conductors `all` of the dimension (provenance
// input).
uint64_t multiplet_from_provenance(const std::string& all, const std::string& conductor);

// Builds one multiplet folder from a verified .sicx and returns it. The SICX itself is not copied.
std::filesystem::path release_layout(const std::filesystem::path& sicx_path,
                                     const std::filesystem::path& root, const ReleaseOptions& opt);

// Release catalogue (schema sic-release-catalogue/1.0) rebuilt from ROOT/d<d>/m<N>/{manifest.json,
// multiplet.sicx.parts.json,...}: dimensions, multiplets, rays and files (path, bytes, SHA-256),
// and for every SICX its parts (Release assets) with SHA-256 and the Merkle roots. Every number is
// a string.
sic::support::Json release_catalogue(const std::filesystem::path& root);

}  // namespace sic::data
