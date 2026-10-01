#include <assert.h>
#include <string.h>
#include "provider_parser.h"

static void test_claude_windows(void) {
  provider_data_t d;
  assert(provider_parse_claude("{\"five_hour\":{\"utilization\":0.42,\"resets_at\":\"2099-01-02T03:04:05.123Z\"},\"seven_day\":{\"utilization\":75,\"resets_at\":\"2099-01-03T03:04:05+00:00\"}}", &d));
  assert(d.remaining[0] == 100 && d.remaining[1] == 25);
  assert(strcmp("reset unknown", d.lines[0]) && strcmp("reset unknown", d.lines[1]));
}
static void test_claude_unknown_and_malformed(void) {
  provider_data_t d;
  assert(provider_parse_claude("{\"five_hour\":null}", &d));
  assert(d.remaining[0] == -1 && d.remaining[1] == -1 && !strcmp("reset unknown", d.lines[0]));
  assert(!provider_parse_claude("{}", &d));
  assert(!provider_parse_claude("{\"five_hour\":17}", &d));
}
static void test_codex_and_cursor(void) {
  provider_data_t d;
  assert(provider_parse_codex("{\"rate_limit\":{\"primary_window\":{\"used_percent\":25,\"reset_after_seconds\":3600},\"secondary_window\":{\"used_percent\":90,\"reset_after_seconds\":7200}}}", &d));
  assert(d.remaining[0] == 75 && d.remaining[1] == 10 && !strcmp("free resets unknown", d.lines[0]));
  assert(!provider_parse_codex("{\"rate_limit\":{\"primary_window\":17}}", &d));
  assert(provider_parse_cursor("{\"individualUsage\":{\"plan\":{\"autoPercentUsed\":0,\"apiPercentUsed\":100},\"onDemand\":{\"used\":123}},\"billingCycleEnd\":\"2099-01-02T03:04:05-05:00\"}", &d));
  assert(d.remaining[0] == 100 && d.remaining[1] == 0 && strcmp("billing reset unknown", d.lines[2]));
}
int main(void) { test_claude_windows(); test_claude_unknown_and_malformed(); test_codex_and_cursor(); return 0; }
