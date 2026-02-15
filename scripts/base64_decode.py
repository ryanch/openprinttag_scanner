import base64
import os

def decode_base64_to_file():
    """
    Asks the user for a base64 string, decodes it, and writes the binary data
    to temp/tag.bin.
    """
    base64_string = input("Please enter the base64 string: ")

    output_dir = "temp"
    output_filepath = os.path.join(output_dir, "tag.bin")

    # Create the temp directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    try:
        # Decode the base64 string
        binary_data = base64.b64decode(base64_string)

        # Write the binary data to the file
        with open(output_filepath, "wb") as f:
            f.write(binary_data)

        print(f"Successfully decoded base64 data and wrote to '{output_filepath}'")

    except base64.binascii.Error as e:
        print(f"Error decoding base64 string: {e}. Please ensure it is a valid base64 string.")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")

if __name__ == "__main__":
    decode_base64_to_file()
