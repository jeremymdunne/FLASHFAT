/**
 * @file FlashFAT.hpp
 * @author Jeremy Dunne (jeremymdunne@gmail.com)
 * @brief Header file for the FLASH FAT Library
 * @version 0.1
 * @date June 2022
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef _FLASH_FAT_HPP_
#define _FLASH_FAT_HPP_

#include <Arduino.h> 
#include "W25Q64FV.hpp"

//#define FLASH_FAT_SERIAL_DEBUG ///< Preprocessor for enabling Serial debugging output 

#define FLASH_FAT_MAX_FILE_COUNT 254    ///< Maximum amount of files allowed
#define FLASH_FAT_FILE_BUFFER 512       ///< Write buffer size. See README for implementation notes
#define FLASH_FAT_NO_ERROR_FILE 255     ///< Not implemented 


/**
 * @brief Structure for a single file 
 * 
 */
typedef struct{
    uint16_t _start_page;       ///< Start page (page is 256)
    uint16_t _page_length;      ///< Length of the file in pages (256 bytes). Inclusive
    uint8_t _end_offset;        ///< End offset on the last page. Not inclusive
}   FlashFAT_file_entry; 

/**
 * @brief Structure for storing the entire contents of the storage system 
 * 
 */
typedef struct{
    uint8_t _num_files;                                     ///< Number of files on the system. 1 indexed
    uint8_t _file_close_err;                                ///< Not implemented yet
    FlashFAT_file_entry _files[FLASH_FAT_MAX_FILE_COUNT];   ///< Allocation for files 
}   FlashFAT_file_allocation_table; 

/**
 * @brief Status return for FlashFAT
 * 
 */
typedef enum{
    FLASHFAT_OK = 0,                            ///< OK 
    FLASHFAT_FLASH_FAILURE,                     ///< Failed to communicate with the Flash Chip     
    FLASHFAT_MAX_FILE_COUNT_REACHED,            ///< Maximum number of files reached. Cannot create more
    FLASHFAT_FILE_ALLOCATION_TABLE_NOT_FOUND,   ///< No FAT table found
    FLASHFAT_WRONG_MODE,                        ///< Library in wrong mode 
    FLASHFAT_INVALID_FILE                       ///< File not available
}   FlashFAT_status_t; 


/**
 * @brief FlashFAT Object
 * 
 * FlashFAT is a file storage system meant to in part mimic the standard FAT system for non-volatile flash chips. 
 * Currently only supports the W25Q64FV chips, can expand to more easily. The FAT table is found at the start of the 
 * storage. Allows for Reading & Writing of files. Currently files cannot be moved or expanded after the fact
 */
class FlashFAT{
public: 
    /**
     * @brief Initialize the FlashFAT system 
     * 
     * Checks for an attached flash chip, checks for a FAT table, creates one if none is found
     * 
     * @param _cs                   Chip-select pin for the Flash Chip
     * @return FlashFAT_status_t    Return status
     */
    FlashFAT_status_t begin(int _cs); 

    /**
     * @brief Opens a file for reading 
     * 
     * Opens a file and enables reading
     * 
     * @pre System must be in NO_MODE  
     * 
     * @param fi                    File index, 0-indexed 
     * @return FlashFAT_status_t    Return Status 
     */
    FlashFAT_status_t open_file(uint fi); 

    /**
     * @brief Close a file 
     * 
     * Closes a file in READ or WRITE mode 
     * 
     * @return FlashFAT_status_t    Return Status 
     */
    FlashFAT_status_t close_file(); 

    /**
     * @brief Creates a new file to write to 
     * 
     * @pre System must be in NO_MODE 
     * 
     * @return FlashFAT_status_t    Return Status 
     */
    FlashFAT_status_t new_file(); 

    /**
     * @brief Get the file allocation table object
     * 
     * @param table                 Pointer to the table to fill out. 
     * @return FlashFAT_status_t    Return status 
     */
    FlashFAT_status_t get_file_allocation_table(FlashFAT_file_allocation_table *table); 

    /**
     * @brief write a buffer
     * 
     * Writes a byte buffer to the current open file
     * 
     * @pre System must be in WRITE_MODE 
     * 
     * @param buffer                Buffer to write 
     * @param length                Length to write 
     * @return FlashFAT_status_t    Return Status 
     */
    FlashFAT_status_t write(byte *buffer, uint length); 

    /**
     * @brief read from the device 
     * 
     * Reads from the currently open file 
     * 
     * @pre System must be in READ_MODE 
     * 
     * @param buffer    Buffer to read into 
     * @param length    Desired number of bytes to read 
     * @return uint     Number of bytes read 
     */
    uint read(byte *buffer, uint length); 

    /**
     * @brief Check the remaining length of the current file 
     * 
     * Checks the remaining length of the file against the current read position 
     * 
     * @pre System must be in READ_MODE 
     * 
     * @return uint     Length remaining
     */
    uint peek(); 

    /**
     * @brief Delete the last file
     * 
     * @return FlashFAT_status_t    Return Status 
     */
    FlashFAT_status_t delete_last_file(); 

    /**
     * @brief Delete all files 
     * 
     * @return FlashFAT_status_t    Return Status  
     */
    FlashFAT_status_t delete_all_files(); 

    /**
     * @brief Create and write a new file allocation table object
     * 
     * @return FlashFAT_status_t    Return Status 
     */
    FlashFAT_status_t create_file_allocation_table(); 

private: 

    /**
     * @brief System mode 
     * 
     */
    typedef enum{
        FLASHFAT_WRITE_MODE,    ///< Writing mode 
        FLASHFAT_READ_MODE,     ///< Reading mode 
        FLASHFAT_NO_MODE        ///< No mode 
    } FLASHFAT_MODE; 

    W25Q64FV _flash;                                ///< Flash Chip Interface Library 
    FlashFAT_file_allocation_table _table;          ///< Local FAT table 
    FLASHFAT_MODE _mode = FLASHFAT_NO_MODE;         ///< Current system mode 
    byte _write_buffer[FLASH_FAT_FILE_BUFFER];      ///< Write buffer 
    uint _write_buffer_index = 0;                   ///< Current index in the write buffer
    uint _erase_index;                              ///< Last 'safe' index to write to 
    uint _current_index;                            ///< Current index being used 
    uint _end_index;                                ///< Last index of the file 
    uint _file_index;                               ///< Index in the FAT that is currently being used

    /**
     * @brief Write a FAT table 
     * 
     * @param table                 Table to write 
     * @return FlashFAT_status_t    Return Status 
     */
    FlashFAT_status_t write_file_allocation_table(FlashFAT_file_allocation_table *table);

    #ifdef FLASH_FAT_SERIAL_DEBUG
        /**
         * @brief Print a buffer to serial 
         * 
         * Formats and prints a byte buffer 
         * 
         * @param buffer    Buffer to write 
         * @param length    Length of the buffer 
         * @param col       Number of columns     
         */
        void print_buffer(byte *buffer, uint length, uint col = 16){
            // print the buffer 
            uint index = 0; 
            for(uint r = 0; r < (length/col + 1); r ++){
                for(uint c = 0; c < col; c ++){
                    uint index = r*col + c; 
                    if(index < length){
                        Serial.print(buffer[index]); 
                        Serial.print('\t'); 
                    }
                    else break; 
                }
                Serial.println(); 
            }
        } 
    #endif 

}; 



#endif 