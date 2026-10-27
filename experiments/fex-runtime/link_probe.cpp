// This target checks linkability only. It does not execute guest code.
extern "C" __attribute__((visibility("default"))) const char* armxbox_fex_probe_kind() {
  return "link-only: guest execution unverified";
}
