"""The mod's calibration and resync (rondelirium.hpp calibrate() and tick()'s sync), for replaying a recording on another
character: a build fitted to the recorded skeleton, each part's frame on its bone, and Delirium's states put together
from the ragdoll as read (its bones, velocities, spins, centres of mass) through that character's joints."""
import ctypes as C


def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def conj(q): return (-q[0], -q[1], -q[2], q[3])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def rotate(q, v):
    r = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), conj(q))
    return (r[0], r[1], r[2])


def compose(a, b): return (add(a[0], rotate(a[1], b[0])), qmul(a[1], b[1]))


def inverse(a):
    qi = conj(a[1]); p = rotate(qi, a[0])
    return ((-p[0], -p[1], -p[2]), qi)


class Cal:
    def __init__(self, lib, base, joints, ref, posed=False):
        """joints: the 63 floats the build was fitted to; ref: 21 bones (p, q) in their reference pose. posed: the fitted
        build standing in the character's own pose (nm_build_with_pose_of), its joints at their zero, not turned to the
        skeleton's."""
        F = lambda v: (C.c_float * len(v))(*v)
        lib.nm_build_with_pose_of.restype = C.c_void_p
        self.build = C.c_void_p(lib.nm_build_fit(base, F(joints)))
        assert self.build, 'nm_build_fit failed'
        lib.nm_build_with_joints_of.restype = C.c_void_p
        if posed is True or posed == 1: self.build = C.c_void_p(lib.nm_build_with_pose_of(self.build, base))
        elif posed: self.build = C.c_void_p(lib.nm_build_with_joints_of(self.build, base, C.c_uint32(int(posed))))  # bit k: part k's joint at zero
        self.off = []
        for k in range(21):
            p = (C.c_float * 3)(); q = (C.c_float * 4)(); lib.nm_build_part_frame(self.build, k, p, q)
            self.off.append(compose(inverse(ref[k]), (tuple(p), tuple(q))))
        probe = C.c_void_p(lib.nm_agent_create(self.build, F([0, 0, 0]), C.c_float(0), C.c_float(0.3)))
        stand = []
        for k in range(21):
            p = (C.c_float * 3)(); q = (C.c_float * 4)(); lib.nm_agent_get_part_transform(probe, k, p, q); stand.append((tuple(p), tuple(q)))
        self.parent = [-1] * 21; self.anchor_parent = [(0, 0, 0)] * 21; self.anchor = [(0, 0, 0)] * 21
        for k in range(1, 21):
            par = C.c_int32(); pv = (C.c_float * 3)(); fr = (C.c_float * 4)(); lm = (C.c_float * 3)(); ar = (C.c_float * 3)()
            lib.nm_agent_get_joint(probe, k, C.byref(par), pv, fr, lm, ar)
            p = par.value; at = tuple(pv)
            self.parent[k] = p
            self.anchor_parent[k] = rotate(conj(stand[p][1]), sub(at, stand[p][0]))
            self.anchor[k] = rotate(conj(stand[k][1]), sub(at, stand[k][0]))
        lib.nm_agent_destroy(probe)

    def states(self, sync):
        """sync: 21 x 16 floats (bone p3 q4, velocity, spin, centre of mass); 21 x 13 states as the mod makes them."""
        bone = [((s[0], s[1], s[2]), (s[3], s[4], s[5], s[6])) for s in sync]
        v = [tuple(s[7:10]) for s in sync]; spin = [tuple(s[10:13]) for s in sync]; com = [tuple(s[13:16]) for s in sync]
        part = [compose(bone[k], self.off[k]) for k in range(21)]
        centre = [None] * 21; vel = [None] * 21
        centre[0] = part[0][0]
        vel[0] = add(v[0], cross(spin[0], sub(part[0][0], com[0])))
        for k in range(1, 21):
            p = self.parent[k]
            fp = rotate(part[p][1], self.anchor_parent[k]); tp = rotate(part[k][1], self.anchor[k])
            centre[k] = sub(add(centre[p], fp), tp)
            vel[k] = sub(add(vel[p], cross(spin[p], fp)), cross(spin[k], tp))
        out = []
        for k in range(21): out += list(centre[k]) + list(part[k][1]) + list(vel[k]) + list(spin[k])
        return out
