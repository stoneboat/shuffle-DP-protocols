import asyncio
import aioipfs

async def add_to_ipfs(file_path):
    # Connect to the IPFS daemon
    async with aioipfs.AsyncIPFS() as client:
        # Read the file content
        with open(file_path, 'rb') as file:
            file_content = file.read()
            # Add the file content to IPFS
            response = await client.add_bytes(file_content)
            # Extract and return the CID (Hash)
            return response['Hash']


async def get_from_ipfs(cid, output_path):
    # Connect to the IPFS daemon
    async with aioipfs.AsyncIPFS() as client:
        # Retrieve the file content using the CID
        file_content = await client.cat(cid)
        # Save the content to the specified output path
        with open(output_path, 'wb') as file:
            file.write(file_content)
            return output_path