from PIL import Image
import os
import io

def gif_to_jpg_frames(gif_path, output_folder, output_size=(240, 240)):
    # Create output folder if it doesn't exist
    os.makedirs(output_folder, exist_ok=True)
    
    # Open the GIF file
    with Image.open(gif_path) as gif:
        # Check if the GIF is animated by looking at the n_frames attribute
        for frame in range(gif.n_frames):
            # Set the current frame and resize it
            gif.seek(frame)
            frame_image = gif.copy().convert("RGB")  # Convert to RGB to save as JPG
            frame_image = frame_image.resize(output_size, Image.LANCZOS)
            
            # Save each frame as a JPG with a sequential name
            output_filename = os.path.join(output_folder, f"frame_{frame+1}.jpg")
            frame_image.save(output_filename, "JPEG")
            print(f"Saved: {output_filename}")

    print("All frames converted and saved as JPGs.")

# Example usage:
gif_to_jpg_frames("test.gif", "gif_images")
