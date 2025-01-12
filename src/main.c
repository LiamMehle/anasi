#include <stdio.h>
#include <stdint.h>
#include <windows.h>

// yes, it's an implementation file
// these functions are gonna get inlined anyway
#include "mem.h"
#include "string.h"
#include "sys.h"

enum EntryType {
	file,
	dir
};
typedef struct {
	String path;
	uint8_t type;
} Entry;

typedef LINKED_LIST_ELEMENT_OF(Entry) EntryElement;
/**
 * I'd make use of an object/freelist arena, but then I'd have to split the
 * master_arena between strings and Entry elements
 */
EntryElement* enumerate_fs_path(String base_path, StackArena* const master_arena) {

	String search_path = string_build_in_stack_arena(master_arena, (String[]){
		base_path,
		SIZED_STRING("\\*.*"),
		{ 0 }
	});

	WIN32_FIND_DATA fd; 
	HANDLE directory_handle = FindFirstFile(search_path.str, &fd);

	// handle built, the string can be freed (which amounts to an integer write)
	stack_arena_empty(master_arena);

	String const current_dir = SIZED_STRING(".");
	String const parent_dir  = SIZED_STRING("..");

	EntryElement* previous_element = NULL;
	EntryElement* first_element = NULL;

	if(directory_handle == INVALID_HANDLE_VALUE)
		return first_element;
	do {
		String filename = SIZED_STRING(fd.cFileName);

		if (string_compare(filename, current_dir)
			|| string_compare(filename, parent_dir))
			continue;

		String const path = string_build_in_stack_arena(master_arena, (String[]){
			base_path,
			SIZED_STRING("\\"),
			filename,
			{ 0 }
		});
		if (!path.str)  // allocation failed, string won't fit in memory
			continue;

		EntryElement* entry = STACK_ARENA_ALLOC(EntryElement, master_arena);
		if (!entry)     // entry allocation failed
			break;      // there's no point iterating further

		*entry = (EntryElement){
			.item = {
				.path = path,
				.type = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? dir : file
			},
			.next = NULL
		};

		// branches bad, yes, but these are very predictable
		if (!first_element)
			first_element = entry;
		if (previous_element)
			previous_element->next = entry;

		previous_element = entry;
	} while (FindNextFile(directory_handle, &fd)); 
	FindClose(directory_handle);
	return first_element;
}

int format_entry(String* const s, Entry const entry) {
	char* entry_type_string[] = {
		"file:",
		"dir: "
	};
	s->len = snprintf(s->str, s->capacity, "%s %s\n", entry_type_string[entry.type], entry.path.str);
	return s->len;
}

int main() {
	// configuration
	size_t const scratch_size = 4096*4;
	// our entire malloc allowance
	sys_init();
	void* true_dynamic_memory = sys_alloc(scratch_size);

	if (!true_dynamic_memory) {
		size_t const last_error = GetLastError();
		return last_error;
	}

	// arena setup to organize malloc use
	StackArena arena = stack_arena_generate(true_dynamic_memory, scratch_size);

	EntryElement const* const first_entry = enumerate_fs_path(SIZED_STRING("."), &arena);

	size_t const scratch_consumed_memory = arena.used;
	printf("\nused %zu bytes", scratch_consumed_memory);

	size_t output_len = 0;
	list_foreach(first_entry, EntryElement, entry)
		output_len += format_entry(&(String){ 0 }, entry->item);

	String output_str = {
		.str = stack_arena_alloc(&arena, output_len, 1),
		.capacity = output_len,
		.len = 0
	};
	String temp_str =  {
		.str = stack_arena_alloc(&arena, 512, 1),
		.capacity = 512,
		.len = 0
	};
	if (!(temp_str.str && output_str.str)) {
		puts("failed to allocate output str memory");
		return 2;
	}

	list_foreach(first_entry, EntryElement, entry) {
		temp_str.len = 0;

		format_entry(&temp_str, entry->item);
		output_str = string_append(output_str, temp_str);
	}

	puts(output_str.str);

	puts("done");
	stack_arena_empty(&arena);
	return 0;
}
