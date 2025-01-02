// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract ARE_INT_Storage {
    uint256[] public value_arr;
    uint256 public arr_len;
    uint256 public cnt;

    // Constructor to initialize the contract with the size of the array
    constructor(uint256 _arr_len) {
        arr_len = _arr_len; // Set the size of the value array
        cnt = 0; // Initialize the counter to 0
        value_arr = new uint256[](arr_len); // Initialize the array with the given size
    }

    // Function to set a value in the array
    function setValue(uint256 value) public {
        require(cnt < value_arr.length, "Storage is full");
        value_arr[cnt] = value;
        cnt = cnt + 1;
    }

    // Function to retrieve the array
    function getStorage() public view returns (uint256[] memory) {
        require(cnt == value_arr.length, "Storage is not ready");
        return value_arr;
    }

    // Function to check the current value of cnt
    function getCurrentCount() public view returns (uint256) {
        return cnt;
    }
}