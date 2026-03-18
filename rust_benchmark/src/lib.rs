use regex::Regex;
use std::ffi::CStr;
use std::os::raw::c_char;
use std::ffi::c_void;

#[no_mangle]
pub extern "C" fn rust_regex_compile(pattern_ptr: *const c_char) -> *mut c_void {
    if pattern_ptr.is_null() {
        return std::ptr::null_mut();
    }
    let pattern_c_str = unsafe { CStr::from_ptr(pattern_ptr) };
    let pattern = match pattern_c_str.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };
    match Regex::new(pattern) {
        Ok(re) => Box::into_raw(Box::new(re)) as *mut c_void,
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn rust_regex_free(ptr: *mut c_void) {
    if !ptr.is_null() {
        unsafe {
            let _ = Box::from_raw(ptr as *mut Regex);
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_regex_match_compiled(
    ptr: *mut c_void,
    text_ptr: *const c_char,
) -> i32 {
    if ptr.is_null() || text_ptr.is_null() {
        return -1;
    }

    let re = unsafe { &*(ptr as *mut Regex) };

    let text_c_str = unsafe { CStr::from_ptr(text_ptr) };
    let text = match text_c_str.to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };

    if re.is_match(text) {
        1
    } else {
        0
    }
}

// Keep old API for non-performance tests if they still use it
#[no_mangle]
pub extern "C" fn rust_regex_match(
    pattern_ptr: *const c_char,
    text_ptr: *const c_char,
) -> i32 {
    if pattern_ptr.is_null() || text_ptr.is_null() {
        return -1;
    }

    let pattern_c_str = unsafe { CStr::from_ptr(pattern_ptr) };
    let text_c_str = unsafe { CStr::from_ptr(text_ptr) };

    let pattern = match pattern_c_str.to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };

    let text = match text_c_str.to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };

    let re = match Regex::new(pattern) {
        Ok(re) => re,
        Err(_) => return -1,
    };

    if re.is_match(text) {
        1
    } else {
        0
    }
}
