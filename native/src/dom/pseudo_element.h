#pragma once

// Generated pseudo-elements are internal layout nodes. They deliberately stay out of the public
// DOM ABI: bindings continue to observe only elements authored in the document.
enum class aether_pseudo_element_t : unsigned char {
    none = 0,
    before,
    after,
    // Recognized syntax only: no first-letter layout node is generated yet.
    first_letter,
};
