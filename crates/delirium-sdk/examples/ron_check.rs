//! RoNDelirium: stand, balance 2 s, take a chest shot, print what happens.
//! ron_check <fred.xrt> <build.txt>, with GTAIV_EXE set.
use delirium_physics::*;
use std::ffi::CString;

fn c(s: &str) -> CString { CString::new(s).unwrap() }

fn main() {
    let a: Vec<String> = std::env::args().collect();
    unsafe {
        assert_eq!(nm_unlock(c(&std::env::var("GTAIV_EXE").unwrap()).as_ptr()), 0);
        assert!(nm_init(c(&a[1]).as_ptr()) >= 0);
        let build = nm_build_load(c(&a[2]).as_ptr());
        let w = nm_world_create();
        if std::env::var("MESH_FLOOR").is_ok() {
            let p = [-50.0f32, -50.0, 0.0, 50.0, -50.0, 0.0, 50.0, 50.0, 0.0, -50.0, 50.0, 0.0];
            nm_world_add_mesh(w, p.as_ptr(), 4, [0u32, 1, 2, 0, 2, 3].as_ptr(), 2, std::ptr::null());
        } else {
            nm_world_add_floor(w, 0.0, 0.0, 0.0, 50.0);
        }
        let g = nm_agent_create(build, [0.0f32; 3].as_ptr(), 0.0, 0.3);
        let report = |t: &str| {
            let mut head = [0.0f32; 3];
            nm_agent_get_part_transform(g, 12, head.as_mut_ptr(), std::ptr::null_mut());
            println!("{t}: balancing {} fallen {} head {:.2?}", nm_agent_is_balancing(g), nm_agent_has_fallen(g), head);
        };
        report("start");
        nm_msg_begin(g, c("bodyBalance").as_ptr());
        nm_msg_set_bool(g, c("start").as_ptr(), 1);
        nm_msg_send(g, w);
        for i in 0..120 {
            nm_agent_step(g, w, 1.0 / 60.0);
            if i % 30 == 29 { report(&format!("{:.1} s", (i + 1) as f32 / 60.0)); }
        }
        let mut chest = [0.0f32; 3];
        nm_agent_get_part_transform(g, 10, chest.as_mut_ptr(), std::ptr::null_mut());
        nm_agent_apply_push(g, chest.as_ptr(), [0.0f32, 40.0, 0.0].as_ptr());
        nm_agent_task_shot(g, w, c("POWERFUL_WEAPON").as_ptr(), 9, chest.as_ptr(), [0.0f32, 1.0, 0.0].as_ptr(), 200.0);
        for i in 0..180 {
            nm_agent_step(g, w, 1.0 / 60.0);
            if i % 30 == 29 { report(&format!("shot +{:.1} s", (i + 1) as f32 / 60.0)); }
        }
    }
}
