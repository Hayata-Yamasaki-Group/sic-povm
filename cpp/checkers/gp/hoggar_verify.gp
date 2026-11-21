\\ Exact check of the Hoggar lines (d = 8, 3-qubit Pauli covariant), independent of the C++ test.
\\   gp -q -f cpp/checkers/gp/hoggar_verify.gp          (run from the repository root; prints PASS or FAIL)
\\ Reads the Gaussian-integer coordinates from data/d8/hoggar/exact.json (v = (-1+2i,1,...,1), psi = v/sqrt(12)).
\\ Convention (docs/data.md#hoggar-covariance-and-exact-proof): |x> = |x1>|x2>|x3>, x = 4 x1 + 2 x2 + x3 (factor 1 = most significant bit);
\\ P_{a,b} = (X^a1 Z^b1)(x)(X^a2 Z^b2)(x)(X^a3 Z^b3), P|x> = (-1)^(b.x) |x xor a>.
\\ Everything below is exact arithmetic in Z[i]; the high-precision decimals of c0.json are checked at 100 digits.

fail(msg) = { print("FAIL: ", msg); quit(1); }

read_all(f) = strjoin(readstr(f), "\n");

txt = read_all("data/d8/hoggar/exact.json");
if(#strsplit(txt, "\"schema\":\"sic-hoggar-exact-json/1.0\"") != 2, fail("unsupported Hoggar exact schema"));
\\ extract the 16 quoted integers after "coordinates_unnormalized":
p = strsplit(txt, "\"coordinates_unnormalized\":[")[2];
p = strsplit(p, "]]")[1];
toks = strsplit(p, "\"");
nums = vector(0);
{ for(i = 1, #toks, if(i % 2 == 0, nums = concat(nums, eval(toks[i])))); }
if(#nums != 16, fail("expected 16 integers"));
v = vector(8, j, nums[2*j-1] + I * nums[2*j]);

dot(u, w) = sum(j = 1, 8, conj(u[j]) * w[j]);
\\ P_{a,b} v : result[x xor a] = (-1)^{popcount(b and x)} v[x] ; vector index = x + 1
pw(a, b, v) = { my(w = vector(8)); for(x = 0, 7, w[bitxor(x, a) + 1] = (-1)^hammingweight(bitand(b, x)) * v[x+1]); w };

n = dot(v, v);
if(n != 12, fail("<v|v> must be 12"));
cnt = 0;
for(k = 1, 63, g = dot(v, pw(k >> 3, k % 8, v)); if(norm(g) * 9 != n^2, fail(Str("overlap of P_", k))); cnt++);
w = vector(64, k, pw((k-1) >> 3, (k-1) % 8, v));
pairs = 0;
for(k = 1, 64, for(l = k+1, 64, if(9 * norm(dot(w[k], w[l])) != n^2, fail(Str("pair ", k, ",", l))); pairs++));
if(cnt != 63 || pairs != 2016, fail("counts"));

\\ the decimals in c0.json: hp mid +- rad encloses the exact coordinates (exact rational comparison)
js = read_all("data/d8/hoggar/c0.json");
hp = strsplit(strsplit(strsplit(strsplit(js, "\"hp\":")[2], "\"values\":")[2], "]]")[1], "\"");
vals = vector(0); { for(i = 1, #hp, if(i % 2 == 0, vals = concat(vals, [hp[i]]))); }
if(#vals != 32, fail("hp has 32 strings"));
dec(s) = { my(neg = (Vec(Str(s))[1] == "-"), t = if(neg, strsplit(s, "-")[2], s), q = strsplit(t, "."), m = eval(q[1]) * 10^#q[2] + eval(q[2])); (if(neg, -1, 1)) * m / 10^#q[2] };
{ for(j = 1, 8,
    re = dec(vals[4*j-3]); rr = dec(vals[4*j-2]); im = dec(vals[4*j-1]); ir = dec(vals[4*j]);
    sg = if(j == 1, -1, 1);
    \\ |re - sg/sqrt(12)| <= rr  <=>  (re - rr)... compare squares of the nonnegative sides exactly
    mg = abs(re);
    if(!(((mg - rr)^2 <= 1/12) && (1/12 <= (mg + rr)^2)) || sign(re) != sg, fail(Str("hp re of coordinate ", j)));
    if(j == 1, mg = abs(im); if(!(((mg - ir)^2 <= 1/3) && (1/3 <= (mg + ir)^2)) || sign(im) != 1, fail("hp im of coordinate 1")),
       if(im != 0 || ir != 0, fail(Str("hp im of coordinate ", j, " must be exactly 0"))));
  ); }
print("PASS: Hoggar lines: <v|v> = 12, 63 overlaps = 1/9, ", pairs, " line pairs equiangular, hp balls enclose the exact values");
quit(0);
