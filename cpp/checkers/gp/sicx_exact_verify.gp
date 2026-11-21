\\ ===================================================================================================
\\ sicx_exact_verify.gp: independent exact verification of a SIC fiducial multiplet from exact.json
\\ (schema sic-exact-json/1.0, docs/sicx.md). Reference implementation in GP; it shares no
\\ code with the C++ library that wrote the file.
\\
\\ Usage:   SIC_EXACT_JSON=path/to/exact.json gp -q -s 2G sicx_exact_verify.gp < /dev/null
\\    or   sicx_file = "path/to/exact.json"; sicx_noauto = 0; read("sicx_exact_verify.gp")
\\ Options (environment or GP variables): SIC_SHIFTS="0,1,2" (sicx_shifts = [0,1,2]) checks only those shifts a
\\ of the autocorrelation test (default: all a in [0, d/2]); SIC_RAY="d8-m2-c0" restricts to one ray.
\\ Works with PARI/GP >= 2.13 (tested: 2.13.3) and in the GP bundled with Sage (untested here).
\\
\\ What is rebuilt and checked, per ray of the file:
\\   * the number field L = K(g_0, ..., g_{t-1}) from the minimal polynomials (omega^2 + c1 omega + c0 over Q, then
\\     monic polynomials with coefficients in the lower fields), with PARI polmods in the variables
\\     sicOm < sicG0 < sicG1 < ...;
\\   * the coordinates psi_0, ..., psi_{d-1} in L from the power-basis coefficients;
\\   * complex conjugation as an exact ring automorphism of L (images of omega and of every g_i are in the file; the
\\     script re-checks the defining relations);
\\   * EXACT: nu = sum_k psi_k conj(psi_k) is fixed by conjugation (real), and
\\         |<psi|D_p psi>|^2 = nu^2/(d+1) for every p != 0, i.e. for the unit vector psi/sqrt(nu)
\\     via the homogeneous autocorrelation form (no roots of unity needed):
\\         u^a_k = conj(psi_k) psi_{k-a},  v^a_k = psi_k conj(psi_{k-a}),  r_a(m) = sum_k u^a_{k+m} v^a_k;
\\         a != 0: r_a(m) = 0 (m != 0) and r_a(0) = nu^2/(d+1);   a = 0: r_0(m) = nu^2/(d+1) (m != 0), r_0(0) = 2 nu^2/(d+1)
\\     (|DFT u^a (b)|^2 = sum_m r_a(m) zeta^{bm}; r_a(-m) = conj r_a(m), r_{-a}(m) = conj r_a(m), so a, m in [0, d/2] suffice);
\\   * NUMERICAL (150 digits, not exact): the embedding is selected from the root balls in the file (closest root
\\     of each level polynomial), nu > 0 and real, conjugation is complex conjugation, and the definition
\\     |sum_k conj(psi_k) psi_{k-a} exp(2 pi i b k/d)|^2 = nu^2/(d+1) is checked for all (a,b) != (0,0).
\\ The convention D_p = tau^(p1 p2) X^p1 Z^p2, tau = -exp(pi i/d), changes <psi|D_p psi> only by a unimodular
\\ factor, so the moduli above are those of the WH convention of the specification.
\\ Soundness note: the exact identities hold in L as built from the file; that the embedding used for positivity
\\ is the one named in the file rests on floating point root selection and is not certified by this script.
\\ Prints "PASS" and exits 0 on success, prints "FAIL" and exits 1 otherwise.
\\ ===================================================================================================

default(realprecision, 150);
sicx_digits = 150;

\\ ---- minimal JSON reader (canonical JSON of the specification: strings, arrays, objects; no escapes) ----------
sj_val() =
{
  my(c);
  sj_ws();
  c = sj_s[sj_p];
  if(c == 123, return(sj_obj()));
  if(c == 91, return(sj_arr()));
  if(c == 34, return(sj_str()));
  error("unsupported JSON token at byte ", sj_p);
}
sj_ws() = while(sj_p <= #sj_s && (sj_s[sj_p] == 32 || sj_s[sj_p] == 10 || sj_s[sj_p] == 13 || sj_s[sj_p] == 9), sj_p++);
sj_str() =
{
  my(q = sj_p + 1, e = sj_p + 1);
  while(sj_s[e] != 34, if(sj_s[e] == 92, error("escape sequences are not used in exact.json")); e++);
  sj_p = e + 1;
  Strchr(sj_s[q..e-1]);
}
sj_arr() =
{
  my(L = List(), c);
  sj_p++; sj_ws();
  if(sj_s[sj_p] == 93, sj_p++; return([]));
  while(1,
    listput(L, sj_val()); sj_ws();
    c = sj_s[sj_p]; sj_p++;
    if(c == 93, break);
    if(c != 44, error("bad JSON array at byte ", sj_p)));
  Vec(L);
}
sj_obj() =
{
  my(M = Map(), k, c);
  sj_p++; sj_ws();
  if(sj_s[sj_p] == 125, sj_p++; return(M));
  while(1,
    sj_ws(); k = sj_str(); sj_ws();
    if(sj_s[sj_p] != 58, error("bad JSON object at byte ", sj_p)); sj_p++;
    mapput(M, k, sj_val()); sj_ws();
    c = sj_s[sj_p]; sj_p++;
    if(c == 125, break);
    if(c != 44, error("bad JSON object at byte ", sj_p)));
  M;
}
sj_parse(text) = { sj_s = Vecsmall(text); sj_p = 1; my(v = sj_val()); v; }
sj_get(M, k) = { if(!mapisdefined(M, k), error("missing key ", k)); mapget(M, k); }
sj_keys(M) = { my(m = Mat(M)); vector(matsize(m)[1], i, m[i, 1]); }

\\ numbers only: strings are validated before eval() so that a hostile file cannot run GP code
sx_num(s) =
{
  my(v = Vecsmall(s), dots = 0, ok = (#v > 0));
  for(i = 1, #v, my(c = v[i]);
    if(c == 46, dots++, if(c == 45 && i == 1 && #v > 1, , if(c < 48 || c > 57, ok = 0))));
  if(!ok || dots > 1, error("not a decimal number: ", s));
  eval(s);
}
sx_rat(s) =   \\ integer or fraction "a/b"
{
  my(v = Vecsmall(s), q = 0);
  for(i = 1, #v, if(v[i] == 47, q = i));
  if(q == 0, return(sx_num(s)));
  sx_num(Strchr(v[1..q-1])) / sx_num(Strchr(v[q+1..#v]));
}

\\ ---- elements: [den, [[x,y], ...]] with integer x, y ----------------------------------------------------------------
sx_elt(J) = [sx_num(sj_get(J, "denominator")), vector(#sj_get(J, "coefficients"), e, my(c = sj_get(J, "coefficients")[e]); [sx_num(c[1]), sx_num(c[2])])];

\\ ---- field and tower ------------------------------------------------------------------------------------------------
sx_setup(J) =
{
  my(fld = sj_get(J, "field"), pc = sj_get(fld, "polynomial_coefficients"), tw = sj_get(J, "tower"));
  sx_t = #tw;
  sx_D0 = sx_num(sj_get(fld, "D0"));
  sx_fc = [sx_rat(pc[1]), sx_rat(pc[2])];                 \\ omega^2 + c1 omega + c0
  sx_Om = varlower("sicOm");
  sx_G = vector(sx_t);
  my(prev = sx_Om);
  for(i = 1, sx_t, sx_G[i] = varhigher(Str("sicG", i-1), prev); prev = sx_G[i]);
  sx_fmod = sx_Om^2 + sx_fc[2]*sx_Om + sx_fc[1];
  sx_deg = vector(sx_t, i, sx_num(sj_get(tw[i], "degree")));
  sx_N = vector(sx_t + 1); sx_N[1] = 1;
  for(i = 1, sx_t, sx_N[i+1] = sx_N[i]*sx_deg[i]);   \\ sx_N[n+1] = [F_n : K]
  sx_mod = vector(sx_t);                               \\ sx_mod[i] = defining polynomial of g_{i-1} (as a polynomial)
  sx_fnum = vector(sx_t);                              \\ coefficient arrays of the level polynomials
  sx_tw = tw;
}

\\ K element from [x, y] / den
sx_K(x, y, den) = Mod((x + y*sx_Om)/den, sx_fmod);

\\ element of F_n from a coefficient vector cv (length sx_N[n+1]) over the monomial basis, divided by den
sx_build(cv, lo, n, den) =
{
  if(n == 0, return(sx_K(cv[lo][1], cv[lo][2], den)));
  my(p = sx_deg[n], M = sx_N[n], s = 0, blk);
  for(k = 0, p - 1,
    blk = sx_build(cv, lo + k*M, n - 1, den);
    if(blk != 0, s += blk * sx_G[n]^k));
  Mod(s, sx_mod[n]);
}
sx_elt_of(E, n) = sx_build(E[2], 1, n, E[1]);

sx_levels() =
{
  my(tw = sx_tw);
  for(i = 1, sx_t,
    my(pol = sj_get(tw[i], "polynomial"), p = sx_deg[i], P = sx_G[i]^p, arr = vector(p));
    sx_fnum[i] = vector(p + 1, k, sx_elt(pol[k]));
    for(k = 1, p, P += sx_elt_of(sx_fnum[i][k], i - 1) * sx_G[i]^(k-1));
    if(sx_elt_of(sx_fnum[i][p+1], i - 1) != 1, error("level ", i-1, ": the polynomial is not monic"));
    sx_mod[i] = P);
}

\\ ---- numerics ---------------------------------------------------------------------------------------------------------
sx_decimal(s) = sx_num(s);
\\ numeric value of an element array of F_n at the roots sx_roots, with monomial values sx_mono (vector over e)
sx_numval(E, n) =
{
  my(den = E[1], cv = E[2], s = 0);
  for(e = 1, #cv, if(cv[e][1] != 0 || cv[e][2] != 0, s += (cv[e][1] + cv[e][2]*sx_omnum) * sx_mono[e]));
  s / den;
}
sx_extend_mono(i) =   \\ monomial values for F_i from those of F_{i-1} (sx_mono has sx_N[i] entries)
{
  my(p = sx_deg[i], M = sx_N[i], old = sx_mono);
  sx_mono = vector(M * p);
  for(k = 0, p - 1, for(j = 1, M, sx_mono[k*M + j] = old[j] * sx_roots[i]^k));
}
sx_embedding(J) =
{
  my(r);
  sx_omnum = if(sx_fc[2] != 0, (1 - sqrt(sx_D0)) / 2, sqrt(sx_D0));   \\ omega^2 - omega + c0 : (1 - sqrt(D0))/2
  if(sx_fc[2] == 0, sx_omnum = sqrt(sx_D0));
  if(abs(sx_omnum^2 + sx_fc[2]*sx_omnum + sx_fc[1]) > 10^(-sicx_digits + 20), error("omega embedding does not satisfy its polynomial"));
  sx_roots = vector(sx_t);
  sx_mono = [1];
  for(i = 1, sx_t,
    my(p = sx_deg[i], co = vector(p + 1), rt = sj_get(sx_tw[i], "root"), mid, rad, rts, best = 0, bd = 1e99, d);
    for(k = 1, p + 1, co[k] = sx_numval(sx_fnum[i][k], i - 1));
    mid = sx_decimal(sj_get(rt, "re")[1]) + I*sx_decimal(sj_get(rt, "im")[1]);
    rad = abs(sx_decimal(sj_get(rt, "re")[2])) + abs(sx_decimal(sj_get(rt, "im")[2]));
    rts = polroots(Pol(Vecrev(co), 'x));
    for(q = 1, #rts, d = abs(rts[q] - mid); if(d < bd, bd = d; best = q));
    if(bd > 2*rad + 10^(-sicx_digits/2), error("level ", i-1, ": no root of the level polynomial lies in the stored root ball"));
    sx_roots[i] = rts[best];
    sx_extend_mono(i));
}

\\ ---- exact Galois-type endomorphism -----------------------------------------------------------------------------------
\\ ring endomorphism sending omega to omimg and g_{j-1} to imgs[j] (all elements of L)
sx_apply(x, omimg, imgs) =
{
  if(type(x) != "t_POLMOD", return(x));
  my(v = variable(x.mod), a = x.pol);
  if(v == sx_Om, return(polcoef(a, 0, sx_Om) + polcoef(a, 1, sx_Om)*omimg));
  my(j = 0);
  for(q = 1, sx_t, if(sx_G[q] == v, j = q));
  if(j == 0, error("unexpected variable"));
  if(type(a) == "t_POL" && variable(a) == v,
    my(s = 0);
    for(k = 0, poldegree(a, v), s += sx_apply(polcoef(a, k, v), omimg, imgs) * imgs[j]^k);
    return(s));
  sx_apply(a, omimg, imgs);
}

sx_conj_setup(J) =
{
  my(cj = sj_get(J, "conjugation"));
  sx_cOm = sx_elt_of(sx_elt(sj_get(cj, "omega")), sx_t);
  sx_cG = vector(sx_t, i, sx_elt_of(sx_elt(sj_get(cj, "generators")[i]), sx_t));
  \\ ring endomorphism: the images satisfy the relations of omega and of every g_i
  if(sx_cOm^2 + sx_fc[2]*sx_cOm + sx_fc[1] != 0, error("conjugation image of omega violates the field relation"));
  for(i = 1, sx_t,
    my(P = sx_mod[i], s = 0);
    for(k = 0, sx_deg[i], s += sx_apply(polcoef(P, k, sx_G[i]), sx_cOm, sx_cG) * sx_cG[i]^k);
    if(s != 0, error("conjugation images violate the relation of level ", i-1)));
  \\ numerical identification with complex conjugation
  my(cp = sx_numconj());
  sx_log("complex conjugation: exact ring automorphism, numerically equal to complex conjugation");
}
sx_numconj() =
{
  my(omc = sx_omnum_of_elt(sx_cOm));
  if(abs(omc - conj(sx_omnum)) > 10^(-sicx_digits/3), error("omega image is not conj(iota(omega))"));
  for(i = 1, sx_t,
    if(abs(sx_numeval_poly(sx_cG[i]) - conj(sx_roots[i])) > 10^(-sicx_digits/3), error("conjugation image of g_", i-1, " is not conj(iota(g))")));
  1;
}
\\ numeric value of a nested polmod at the embedding
sx_numeval_poly(x) =
{
  if(type(x) != "t_POLMOD", return(x));
  my(v = variable(x.mod), a = x.pol);
  if(v == sx_Om, return(polcoef(a, 0, sx_Om) + polcoef(a, 1, sx_Om)*sx_omnum));
  my(j = 0);
  for(q = 1, sx_t, if(sx_G[q] == v, j = q));
  if(type(a) == "t_POL" && variable(a) == v,
    my(s = 0);
    for(k = 0, poldegree(a, v), s += sx_numeval_poly(polcoef(a, k, v)) * sx_roots[j]^k);
    return(s));
  sx_numeval_poly(a);
}
sx_omnum_of_elt(x) = sx_numeval_poly(x);

sx_log(s) = print("[", strprintf("%.1f", getabstime()/1000.), " s] ", s);

\\ ---- verification of one ray ---------------------------------------------------------------------------------------------
sx_verify_ray(id, R, d, shifts) =
{
  my(co = sj_get(R, "coordinates"), ps, cp, nu, nunum, fails = 0, checks = 0, target);
  if(#co != d, error("ray ", id, ": expected ", d, " coordinates"));
  ps = vector(d, k, sx_elt_of(sx_elt(co[k]), sx_t));
  cp = vector(d, k, sx_apply(ps[k], sx_cOm, sx_cG));
  sx_log(Str("ray ", id, ": ", d, " coordinates built"));
  nu = sum(k = 1, d, ps[k]*cp[k]);
  checks++; if(sx_apply(nu, sx_cOm, sx_cG) != nu, fails++; print("FAIL: nu is not real"));
  nunum = sx_numeval_poly(nu);
  checks++; if(!(real(nunum) > 0 && abs(imag(nunum)) < 10^(-sicx_digits/3)), fails++; print("FAIL: nu is not positive"));
  sx_log(Str("norm^2 nu = ", strprintf("%.30Ps", real(nunum)), " (exactly real, numerically positive)"));
  target = nu^2/(d+1);
  if(shifts == 0, shifts = [0..d/2]);
  foreach(shifts, a,
    my(u = vector(d, k, cp[k]*ps[(k-1-a)%d+1]), v = vector(d, k, ps[k]*cp[(k-1-a)%d+1]));
    for(m = 0, d/2,
      my(r = sum(k = 0, d-1, u[(k+m)%d+1]*v[k+1]), want = 0);
      if(m == 0, want = if(a == 0, 2*target, target), if(a == 0, want = target));
      checks++;
      if(r != want, fails++; print("FAIL: autocorrelation a=", a, " m=", m)));
    sx_log(Str("shift a = ", a, " done")));
  \\ floating point cross-check from the definition (explicit roots of unity), all (a, b) != (0, 0)
  my(z = exp(2*Pi*I/d), pn = vector(d, k, sx_numeval_poly(ps[k])), nn = sum(k = 1, d, abs(pn[k])^2), worst = 0);
  for(a = 0, d-1, for(b = 0, d-1, if(a == 0 && b == 0, next);
    my(s = sum(k = 0, d-1, conj(pn[k+1])*pn[(k-a)%d+1]*z^(b*k)));
    worst = max(worst, abs(abs(s)^2/nn^2 - 1/(d+1)))));
  sx_log(Str("ray ", id, ": ", checks, " exact identities, ", fails, " failed; floating-point max deviation ", strprintf("%.3e", worst)));
  [fails == 0 && worst < 10^(-30), checks, worst];
}

sx_run() =
{
  my(file = if(type(sicx_file) == "t_STR", sicx_file, getenv("SIC_EXACT_JSON")), t0 = getabstime());
  if(type(file) != "t_STR" || file == "", error("set SIC_EXACT_JSON or sicx_file to the path of exact.json"));
  my(J = sj_parse(readstr(file)[1]), shifts = 0, only = getenv("SIC_RAY"));
  \\ readstr returns a vector of lines; the file is a single line
  if(sj_get(J, "schema") != "sic-exact-json/1.0", error("unsupported schema ", sj_get(J, "schema")));
  if(#sj_get(J, "required_features"), error("file requires unsupported features"));
  my(d = sx_num(sj_get(J, "d")));
  my(sh = getenv("SIC_SHIFTS")); if(type(sh) == "t_STR" && sh != "", shifts = eval(Str("[", sh, "]")));
  if(type(sicx_shifts) == "t_VEC", shifts = sicx_shifts);
  sx_setup(J);
  sx_levels();
  sx_log(Str("d = ", d, ", D0 = ", sx_D0, ", tower degrees ", sx_deg, ", [L:K] = ", sx_N[sx_t+1]));
  sx_embedding(J);
  sx_conj_setup(J);
  my(rays = sj_get(J, "rays"), ids = sj_keys(rays), ok = 1, n = 0);
  foreach(ids, id,
    if(type(only) == "t_STR" && only != "" && only != id, next);
    my(res = sx_verify_ray(id, sj_get(rays, id), d, shifts));
    n++; ok = ok && res[1];
    print(id, ": ", if(res[1], "EXACT CHECKS PASSED", "FAILED")));
  if(n == 0, error("no ray was checked"));
  print("d = ", d, ": ", n, " ray(s); norm and |<psi|D_p psi>|^2 = 1/(d+1) for all p != 0 ", if(ok, "VERIFIED EXACTLY", "NOT verified"),
        if(type(shifts) == "t_VEC" && shifts != 0, " (partial: shifts only)", ""), "; total ", strprintf("%.1f", (getabstime() - t0)/1000.), " s");
  print(if(ok, "PASS", "FAIL"));
  ok;
}

{
  if(type(sicx_noauto) != "t_INT" || sicx_noauto == 0,
    iferr(quit(if(sx_run(), 0, 1)), E, print("ERROR ", E); print("FAIL"); quit(2)));
}
