// PROJ 4 API compatibility header (third-party shim, not Apollo code).
//
// modules/map/hdmap/adapter/xml_parser/coordinate_convert_tool.h includes the
// old PROJ 4 <proj_api.h>, which PROJ 8 removed; Debian ships PROJ 9 with only
// <proj.h>. This header provides the five pj_* calls Apollo uses on top of the
// PROJ 9 API so the upstream OpenDRIVE adapter compiles unmodified. It is put
// first on the include path for the map sources only.
#pragma once
// coordinate_convert_tool.h defines this before the include; proj.h refuses it.
#undef ACCEPT_USE_OF_DEPRECATED_PROJ_API_H
#include <proj.h>

#include <cmath>
#include <string>

#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943296
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082321
#endif
typedef PJ *projPJ;
static inline projPJ pj_init_plus(const char *def) {
  std::string s(def ? def : "");
  if (s.find("+type=crs") == std::string::npos) s += " +type=crs";
  return proj_create(PJ_DEFAULT_CTX, s.c_str());
}
static inline void pj_free(projPJ p) {
  if (p) proj_destroy(p);
}
static inline int pj_is_latlong(projPJ p) {
  PJ_TYPE t = proj_get_type(p);
  return t == PJ_TYPE_GEOGRAPHIC_2D_CRS || t == PJ_TYPE_GEOGRAPHIC_3D_CRS;
}
// Like PROJ 4: geographic coordinates are in radians on both sides.
static inline int pj_transform(projPJ src, projPJ dst, long count, int,
                               double *x, double *y, double *z) {
  PJ *op = proj_create_crs_to_crs_from_pj(PJ_DEFAULT_CTX, src, dst, nullptr,
                                          nullptr);
  if (!op) return -1;
  PJ *norm = proj_normalize_for_visualization(PJ_DEFAULT_CTX, op);
  proj_destroy(op);
  if (!norm) return -1;
  int rc = 0;
  for (long i = 0; i < count; ++i) {
    double xi = x[i], yi = y[i];
    if (pj_is_latlong(src)) {
      xi *= RAD_TO_DEG;
      yi *= RAD_TO_DEG;
    }
    PJ_COORD c = proj_coord(xi, yi, z ? z[i] : 0, 0);
    PJ_COORD r = proj_trans(norm, PJ_FWD, c);
    if (!std::isfinite(r.xyz.x) || !std::isfinite(r.xyz.y)) {
      rc = -1;
      break;
    }
    if (pj_is_latlong(dst)) {
      r.xyz.x *= DEG_TO_RAD;
      r.xyz.y *= DEG_TO_RAD;
    }
    x[i] = r.xyz.x;
    y[i] = r.xyz.y;
    if (z) z[i] = r.xyz.z;
  }
  proj_destroy(norm);
  return rc;
}
