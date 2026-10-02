#include "provider_parser.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "cJSON.h"

static void init_data(provider_data_t *out) {
  memset(out, 0, sizeof(*out));
  out->available = true;
  out->remaining[0] = -1;
  out->remaining[1] = -1;
}

static int remaining(double used) {
  if (!isfinite(used)) return -1;
  if (used <= 0.0) return 100;
  if (used >= 100.0) return 0;
  int value = (int)(100.0 - used + 0.5);
  return value < 0 ? 0 : value > 100 ? 100 : value;
}

/* Civil date to Unix seconds; avoids mktime() inheriting the device timezone. */
static int64_t days_from_civil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  int era = (year >= 0 ? year : year - 399) / 400;
  unsigned yoe = (unsigned)(year - era * 400);
  unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static bool iso8601_utc_seconds(const char *value, int64_t *out) {
  int year, month, day, hour, minute, second;
  int parsed = 0;
  static const unsigned char month_days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
  if (!value || sscanf(value, "%d-%d-%dT%d:%d:%d%n", &year, &month, &day,
                       &hour, &minute, &second, &parsed) != 6 ||
      year < 1970 || month < 1 || month > 12 || day < 1 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return false;
  unsigned max_day = month_days[month];
  if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) max_day = 29;
  if (day > (int)max_day) return false;
  const char *p = value + parsed;
  if (*p == '.') { if (*++p < '0' || *p > '9') return false; while (*p >= '0' && *p <= '9') ++p; }
  int offset = 0;
  if (*p == 'Z') ++p;
  else if ((*p == '+' || *p == '-') && p[1] >= '0' && p[1] <= '9' &&
           p[2] >= '0' && p[2] <= '9' && p[3] == ':' && p[4] >= '0' && p[4] <= '9' &&
           p[5] >= '0' && p[5] <= '9') {
    int hours = (p[1] - '0') * 10 + p[2] - '0';
    int minutes = (p[4] - '0') * 10 + p[5] - '0';
    if (hours > 23 || minutes > 59) return false;
    offset = hours * 3600 + minutes * 60;
    if (*p == '+') offset = -offset;
    p += 6;
  } else return false;
  if (*p) return false;
  *out = days_from_civil(year, (unsigned)month, (unsigned)day) * 86400 +
         hour * 3600 + minute * 60 + second + offset;
  return true;
}

static const cJSON *field(const cJSON *object, const char *a, const char *b) {
  if (!cJSON_IsObject(object)) return NULL;
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, a);
  return value ? value : b ? cJSON_GetObjectItemCaseSensitive(object, b) : NULL;
}

static void reset_text(const cJSON *window, char *out, size_t n) {
  const cJSON *after = field(window, "reset_after_seconds", NULL);
  int64_t seconds = cJSON_IsNumber(after) ? (int64_t)after->valuedouble : -1;
  if (seconds < 0) {
    const cJSON *at = field(window, "reset_at", "resets_at");
    time_t now = time(NULL);
    int64_t timestamp = 0;
    if (cJSON_IsNumber(at)) timestamp = (int64_t)at->valuedouble;
    else if (cJSON_IsString(at)) iso8601_utc_seconds(at->valuestring, &timestamp);
    if (timestamp && now > 100000) seconds = timestamp - now;
  }
  if (seconds < 0) {
    strlcpy(out, "reset unknown", n);
  } else {
    if (seconds < 86400) snprintf(out, n, "reset %lldh %lldm", (long long)(seconds / 3600), (long long)((seconds % 3600) / 60));
    else snprintf(out, n, "reset %lldd %lldh", (long long)(seconds / 86400), (long long)((seconds % 86400) / 3600));
  }
}

static bool parse_json(const char *body, provider_data_t *out,
                       bool (*fill)(const cJSON *, provider_data_t *)) {
  if (!body || !out) return false;
  cJSON *root = cJSON_Parse(body);
  if (!cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
  init_data(out);
  bool ok = fill(root, out);
  cJSON_Delete(root);
  return ok;
}

static bool fill_codex(const cJSON *root, provider_data_t *out) {
  const cJSON *rate = field(root, "rate_limit", NULL);
  const cJSON *primary = field(rate, "primary_window", NULL);
  const cJSON *weekly = field(rate, "secondary_window", NULL);
  if (!cJSON_IsObject(rate)) return false;
  if ((primary && !cJSON_IsObject(primary) && !cJSON_IsNull(primary)) ||
      (weekly && !cJSON_IsObject(weekly) && !cJSON_IsNull(weekly))) return false;
  const cJSON *used = field(primary, "used_percent", NULL);
  if (cJSON_IsNumber(used)) out->remaining[0] = remaining(used->valuedouble);
  used = field(weekly, "used_percent", NULL);
  if (cJSON_IsNumber(used)) out->remaining[1] = remaining(used->valuedouble);
  const cJSON *credits = field(root, "rate_limit_reset_credits", NULL);
  const cJSON *count = field(credits, "available_count", NULL);
  if (cJSON_IsNumber(count)) snprintf(out->lines[0], sizeof(out->lines[0]), "%d free resets", count->valueint);
  else strlcpy(out->lines[0], "free resets unknown", sizeof(out->lines[0]));
  reset_text(primary, out->lines[1], sizeof(out->lines[1]));
  reset_text(weekly, out->lines[2], sizeof(out->lines[2]));
  return true;
}

static bool fill_cursor(const cJSON *root, provider_data_t *out) {
  const cJSON *individual = field(root, "individualUsage", "individual_usage");
  const cJSON *plan = field(individual, "plan", NULL);
  if (!cJSON_IsObject(individual) || !cJSON_IsObject(plan)) return false;
  const cJSON *used = field(plan, "autoPercentUsed", "auto_percent_used");
  if (cJSON_IsNumber(used)) out->remaining[0] = remaining(used->valuedouble);
  used = field(plan, "apiPercentUsed", "api_percent_used");
  if (cJSON_IsNumber(used)) out->remaining[1] = remaining(used->valuedouble);
  const cJSON *on_demand = field(individual, "onDemand", "on_demand");
  used = field(on_demand, "used", NULL);
  if (cJSON_IsNumber(used)) snprintf(out->lines[0], sizeof(out->lines[0]), "$%.2f used", used->valuedouble / 100.0);
  else strlcpy(out->lines[0], "credits unknown", sizeof(out->lines[0]));
  const cJSON *team_usage = field(root, "teamUsage", "team_usage");
  const cJSON *team = field(team_usage, "onDemand", "on_demand");
  const cJSON *left = field(team, "remaining", NULL);
  if (cJSON_IsNumber(left)) snprintf(out->lines[1], sizeof(out->lines[1]), "$%.0f team left", left->valuedouble / 100.0);
  else strlcpy(out->lines[1], "team cap unknown", sizeof(out->lines[1]));
  const cJSON *cycle = field(root, "billingCycleEnd", "billing_cycle_end");
  int64_t end = 0;
  if (cJSON_IsString(cycle)) iso8601_utc_seconds(cycle->valuestring, &end);
  time_t now = time(NULL);
  if (end && now > 100000) {
    int64_t seconds = end - now;
    if (seconds < 0) seconds = 0;
    snprintf(out->lines[2], sizeof(out->lines[2]), "reset %lldd %lldh", (long long)(seconds / 86400),
             (long long)((seconds % 86400) / 3600));
  } else strlcpy(out->lines[2], "billing reset unknown", sizeof(out->lines[2]));
  return true;
}

/* Anthropic OAuth usage returns five_hour and seven_day windows. */
static bool fill_claude(const cJSON *root, provider_data_t *out) {
  const cJSON *five = field(root, "five_hour", "five_hour_window");
  const cJSON *seven = field(root, "seven_day", "seven_day_window");
  if ((five && !cJSON_IsObject(five) && !cJSON_IsNull(five)) ||
      (seven && !cJSON_IsObject(seven) && !cJSON_IsNull(seven))) return false;
  const cJSON *used = field(five, "utilization", "used_percent");
  if (cJSON_IsNumber(used)) out->remaining[0] = remaining(used->valuedouble);
  used = field(seven, "utilization", "used_percent");
  if (cJSON_IsNumber(used)) out->remaining[1] = remaining(used->valuedouble);
  reset_text(five, out->lines[0], sizeof(out->lines[0]));
  reset_text(seven, out->lines[1], sizeof(out->lines[1]));
  strlcpy(out->lines[2], "Claude OAuth", sizeof(out->lines[2]));
  return five != NULL || seven != NULL;
}

bool provider_parse_codex(const char *body, provider_data_t *out) { return parse_json(body, out, fill_codex); }
bool provider_parse_cursor(const char *body, provider_data_t *out) { return parse_json(body, out, fill_cursor); }
bool provider_parse_claude(const char *body, provider_data_t *out) { return parse_json(body, out, fill_claude); }
