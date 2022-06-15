#include "FlashFAT.hpp"

FlashFAT_status_t FlashFAT::begin(int _cs){
    // open the flash device 
    W25Q64FV_status_t flash_status = _flash.begin(_cs); 
    if(flash_status != W25Q64FV_OK) return FLASHFAT_FLASH_FAILURE; 
    // attempt to read the FAT table 
    FlashFAT_status_t status = get_file_allocation_table(&_table); 
    if(status == FLASHFAT_FILE_ALLOCATION_TABLE_NOT_FOUND){
        // make the table 
        create_file_allocation_table();
    }
    return FLASHFAT_OK; 
}

FlashFAT_status_t FlashFAT::get_file_allocation_table(FlashFAT_file_allocation_table *table){
    // read the file allocation table from the device 
    /* 
        The File Allocation Table is always located at the front of the device 
        It is identified as having the prefix 'FLASHFAT' in the first 8 bytes
        If this is not found, there is no FAT. 
    */ 

    byte buffer[256]; 
    _flash.wait_until_free();
    W25Q64FV_status_t status = _flash.read_page(0, buffer); 
    if(status != W25Q64FV_OK) return FLASHFAT_FLASH_FAILURE; 
    // read the first sequence 
    char comp[8]; 
    memcpy(comp, buffer, 8); 

     #ifdef FLASH_FAT_SERIAL_DEBUG
        Serial.println("FLASHFAT FAT TABLE READ: "); 
        print_buffer(buffer, 256); 
    #endif 

    if(strncmp(comp,"FLASHFAT",8) != 0){
        #ifdef FLASH_FAT_SERIAL_DEBUG
            Serial.println("FLASHFAT NO FAT FOUND"); 
        #endif 
        return FLASHFAT_FILE_ALLOCATION_TABLE_NOT_FOUND; 
    }
    uint index = 8; 
    table->_num_files = buffer[index]; 
    index ++; 
    table->_file_close_err = buffer[index]; 
    index ++; 

    for(uint i = 0; i < table->_num_files; i ++){
        table->_files[i]._start_page = buffer[index]<<8 | buffer[index+1];
        index += 2; 
        table->_files[i]._page_length = buffer[index]<<8 | buffer[index+1];
        index += 2;
        table->_files[i]._end_offset = buffer[index]; 
        index ++;  
    }

    return FLASHFAT_OK; 
} 

FlashFAT_status_t FlashFAT::write_file_allocation_table(FlashFAT_file_allocation_table *table){
    // write the FAT table 
    
    // construct the buffer 
    byte buffer[256]; 
    // zero out the buffer 
    memset(buffer, 0, 256); 
    // start constructing the buffer 
    uint index = 0; 
    // write the 'FLASHFAT' as the first characters 
    char prefix[] = "FLASHFAT"; 
    memcpy(buffer, prefix, 8); 
    index = 8; 
    buffer[index] = table->_num_files; 
    index ++; 
    buffer[index] = table->_file_close_err; 
    index ++; 
    for(int i = 0; i < table->_num_files; i++){
        buffer[index] = table->_files[i]._start_page >> 8; 
        buffer[index+1] = table->_files[i]._start_page; 
        index += 2; 
        buffer[index] = table->_files[i]._page_length >> 8; 
        buffer[index+1] = table->_files[i]._page_length; 
        index += 2; 
        buffer[index] = table->_files[i]._end_offset; 
        index ++; 
    }
    // write the buffer
    _flash.wait_until_free();  
    _flash.enable_writing();
    W25Q64FV_status_t flash_status = _flash.wait_until_free(); 
    if(flash_status != W25Q64FV_OK){
        #ifdef FLASH_FAT_SERIAL_DEBUG
            Serial.println("FLASHFAT CHIP FAILED TO BE FREE"); 
        #endif 
    }
    flash_status = _flash.erase_sector(0); 
    if(flash_status != W25Q64FV_OK){
        #ifdef FLASH_FAT_SERIAL_DEBUG
            Serial.println("FLASHFAT CHIP FAILED TO ERASE"); 
        #endif 
    }
    _flash.wait_until_free(); 
    _flash.enable_writing();
    _flash.wait_until_free();
    flash_status = _flash.write_page(0, buffer); 
    if(flash_status != W25Q64FV_OK){
        #ifdef FLASH_FAT_SERIAL_DEBUG
            Serial.println("FLASHFAT CHIP FAILED TO WRITE FAT TABLE"); 
        #endif 
    }

    #ifdef FLASH_FAT_SERIAL_DEBUG
        Serial.println("FLASHFAT WRITING FAT TABLE: "); 
        print_buffer(buffer, 256); 
    #endif 
    return FLASHFAT_OK; 
} 

FlashFAT_status_t FlashFAT::new_file(){
    // create a new file 
    // check mode 
    if(_mode != FLASHFAT_NO_MODE) return FLASHFAT_WRONG_MODE; 
    // check for space 
    if(_table._num_files >= FLASH_FAT_MAX_FILE_COUNT){
        return FLASHFAT_MAX_FILE_COUNT_REACHED; 
    }
    // find the next available address 
    // use the next available 4kB sector 
    uint32_t last_used_address; 
    if(_table._num_files == 0) last_used_address = 0;
    else{
        last_used_address = (_table._files[_table._num_files-1]._start_page + _table._files[_table._num_files-1]._page_length) * 256 + _table._files[_table._num_files-1]._end_offset; 
    }
    // find the next 4kb address 
    uint32_t next_start_address = ((last_used_address >> 12) + 1) << 12; 
    // ToDo: check memory space 
    _table._num_files ++; 
    _file_index = _table._num_files - 1; 
    _table._files[_file_index]._start_page = next_start_address >> 8; 
    // erase the first 4kB to write stuff 
    _flash.wait_until_free(); 
    _flash.erase_sector(next_start_address); 
    _erase_index = next_start_address + 4095; 
    _current_index = next_start_address; 
    // set the error flag 
    _table._file_close_err = _file_index; 
    // write the FAT table 
    _mode = FLASHFAT_WRITE_MODE; 
    return write_file_allocation_table(&_table); 
}

FlashFAT_status_t FlashFAT::close_file(){
    // close out the file 
    // close out the remaining buffer 
    if(_mode == FLASHFAT_WRITE_MODE){
        
        if(_write_buffer_index != 0){
            // fill up the rest of the buffer as '255'
            memset(&_write_buffer[_write_buffer_index], 255, FLASH_FAT_FILE_BUFFER - _write_buffer_index); 
            // write the buffer 
            // check the erase 
            if(_current_index + FLASH_FAT_FILE_BUFFER > _erase_index){
                // need to erase more 
                _flash.erase_sector(_erase_index+1); 
                // update erase index 
                _erase_index += 4096; 
            }
            // check how many pages to write 
            uint pages_to_write = (_write_buffer_index + 255)/256; 
            // Serial.print("Pages to write: ");
            // Serial.println(pages_to_write);
            // write the page 
            for(uint p = 0; p < pages_to_write; p ++){
                // wait until free 
                uint bytes_to_write = 256; 
                if(p == pages_to_write - 1){
                    // check how much to actually write 
                    // need to catch the '0' buffer case 
                    bytes_to_write = _write_buffer_index%256; 
                    // check its not an even 256 bytes 
                    if(bytes_to_write == 0 && _write_buffer_index != 0){
                        // not really clean, should revisit 
                        bytes_to_write = 256; 
                    }
                    // Serial.print("Bytes to write: "); 
                    // Serial.println(bytes_to_write);
                }
                _flash.wait_until_free(); 
                _flash.enable_writing(); 
                _flash.wait_until_free();
                W25Q64FV_status_t status = _flash.write_page(_current_index, &_write_buffer[p*256]); 
                if(status != W25Q64FV_OK) return FLASHFAT_FLASH_FAILURE; 
                _current_index += bytes_to_write; 
            }
        }
        // close out the FAT 
        // Serial.print("End Address: "); 
        // Serial.println(_current_index); 
        // Serial.print("Start Address: ");
        // Serial.println(_table._files[_file_index]._start_page * 256); 

        _table._file_close_err = FLASH_FAT_NO_ERROR_FILE; 
        _table._files[_file_index]._page_length = (_current_index)/256 - _table._files[_file_index]._start_page; 
        _table._files[_file_index]._end_offset = _write_buffer_index%256; // not 100% sure about this? 
        // Serial.print("Page Length: "); 
        // Serial.println(_table._files[_file_index]._page_length);
        write_file_allocation_table(&_table); 
        // set the mode 
    }
    _mode = FLASHFAT_NO_MODE; 
    _write_buffer_index = 0; 
    _file_index = 0; 
    _erase_index = 0; 
    _current_index = 0; 
    return FLASHFAT_OK; 
}



FlashFAT_status_t FlashFAT::write(byte *buffer, uint length){
    // check the mode 
    if(_mode != FLASHFAT_WRITE_MODE) return FLASHFAT_WRONG_MODE; 
    // use a buffer to handle all writing operations 

    for(uint i = 0; i < length; i ++){
        // add to the master buffer 
        _write_buffer[_write_buffer_index] = buffer[0]; 
        _write_buffer_index ++;
        *buffer++;  
        // check sizing 
        if(_write_buffer_index >= FLASH_FAT_FILE_BUFFER){
            // write operations 
            // check the erase 
            if(_current_index + FLASH_FAT_FILE_BUFFER > _erase_index){
                // need to erase more 
                _flash.erase_sector(_erase_index+1); 
                // update erase index 
                _erase_index += 4096; 
            }
            // check how many pages to write 
            uint pages_to_write = FLASH_FAT_FILE_BUFFER/256; 
            // write the page 
            for(uint p = 0; p < pages_to_write; p ++){
                // wait until free 
                _flash.wait_until_free(); 
                _flash.enable_writing(); 
                _flash.wait_until_free(); 
                _flash.write_page(_current_index, &_write_buffer[p * 256]); 
                _current_index += 256; 
            }
            // reset the index 
            _write_buffer_index = 0; 
        }
    }
    return FLASHFAT_OK; 
}

FlashFAT_status_t FlashFAT::open_file(uint fi){
    // check the mode 
    if(_mode != FLASHFAT_NO_MODE){
        // bad situation, error out 
        return FLASHFAT_WRONG_MODE; 
    }
    // update the table 
    FlashFAT_status_t fat_status = get_file_allocation_table(&_table);
    if(fat_status != FLASHFAT_OK){
        return fat_status; 
    }
    // check for a valid file 
    if(_table._num_files <= fi){
        return FLASHFAT_INVALID_FILE; 
    }
    
    _mode = FLASHFAT_READ_MODE; 
    // get the file information 
    _file_index = fi; 
    _current_index = _table._files[_file_index]._start_page * 256; 
    //if(_table._files[_file_index]._page_length > 0){
    _end_index = _current_index + (_table._files[_file_index]._page_length) * 256 + _table._files[_file_index]._end_offset; 
    //}
    // else{
    // _end_index = _current_index; 
    // }
    // Serial.print("File Opened! Start Address: " ); 
    // Serial.print(_current_index); 
    // Serial.print(" end address: "); 
    // Serial.println(_end_index); 
    return FLASHFAT_OK; 
}

uint FlashFAT::read(byte *buffer, uint length){
    // check the mode 
    if(_mode != FLASHFAT_READ_MODE) return FLASHFAT_WRONG_MODE; 
    // check there is actually more to read 
    if(peek() == 0) return 0; 
    // check the size 
    if(_current_index + length > _end_index){
        // adjust length 
        length = _end_index - _current_index; 
    }
    uint length_to_read = length; 
    // do a circular read on the contents 
    byte read_buffer[256]; 
    memset(read_buffer,0,256);
    uint pages_req = (length/256 + 0.5); 
    while(length > 0){
        // read a page 
        
        W25Q64FV_status_t status = _flash.read_page(_current_index, read_buffer);
        _current_index += length; 
        if(status != W25Q64FV_OK) return 0; 
        // increment 
        if(length > 255){
            memcpy(buffer, read_buffer, 256); 
            buffer += 256; 
            length -=256; 
        } 
        else{
            memcpy(buffer, read_buffer, length); 
            buffer += length; 
            length = 0; 
        }
    }
    return length_to_read; //TODO sometime I should fix this   
}

uint FlashFAT::peek(){
    // return the remaining file size 
    if(_mode != FLASHFAT_READ_MODE) return 0; 
    // calculate the remaining file length 
    // Serial.println("Peek: ");
    // Serial.print("End Index: "); 
    // Serial.println(_end_index); 
    // Serial.print("Current: "); 
    // Serial.println(_current_index); 
    uint remaining = _end_index - _current_index; 
    return remaining; 
}

FlashFAT_status_t FlashFAT::delete_last_file(){
    // decrease the page count by one 
    // check the mode 
    if(_mode != FLASHFAT_NO_MODE) return FLASHFAT_INVALID_FILE; 
    // decrease the file count 
    if(_table._num_files > 0) _table._num_files --; 
    return write_file_allocation_table(&_table); 
}

FlashFAT_status_t FlashFAT::delete_all_files(){
    // decrease the page count by one 
    // check the mode 
    if(_mode != FLASHFAT_NO_MODE) return FLASHFAT_INVALID_FILE; 
    // decrease the file count 
    _table._num_files = 0; 
    return write_file_allocation_table(&_table); 
}


FlashFAT_status_t FlashFAT::create_file_allocation_table(){
    // create a blank FAT table 
    _table._num_files = 0; 
    return write_file_allocation_table(&_table); 
}










