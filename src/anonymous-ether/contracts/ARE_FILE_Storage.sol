// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract ARE_FILE_Storage {
    mapping(uint256 => string) public cid_arr;
    uint256 public arr_len;
    uint256 public cnt;

    // Constructor to initialize the contract with the size of the array
    constructor(uint256 _arr_len) {
        arr_len = _arr_len; // Set the size of the value array
        cnt = 0; // Initialize the counter to 0
    }

    // Function to set a value in the array
    function setCID(string memory _cid) public {
        require(cnt < arr_len, "Storage is full");
        cid_arr[cnt] = _cid;
        cnt = cnt + 1;
    }

    // Function to retrieve the array
    function getStorage() public view returns (string[] memory) {
        require(cnt == arr_len, "Storage is not ready");
        string[] memory cids = new string[](arr_len);
        for (uint256 i = 0; i < arr_len; i++) {
            cids[i] = cid_arr[i];
        }
        return cids;
    }

    // Function to check the current value of cnt
    function getCurrentCount() public view returns (uint256) {
        return cnt;
    }
}