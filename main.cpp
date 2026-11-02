// This is a chopped Pong example from SFML examples

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <sfml/Graphics.hpp>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <execution>
#include <mutex>
#include <atomic>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace fs = std::filesystem;

// Helper structure for RGBA pixels (a is safe to ignore for this coursework)
struct rgba_t
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

// Helper function to load RGB data from a file, as a contiguous array (row-major) of RGB triplets, where each of R,G,B is a uint8_t and ranges from 0 to 255
std::vector<rgba_t> load_rgb(const char * filename, int& width, int& height)
{
    int n;
    unsigned char *data = stbi_load(filename, &width, &height, &n, 4);
    const rgba_t* rgbadata = (rgba_t*)(data);
    std::vector<rgba_t> vec;
    vec.assign(rgbadata, rgbadata +width*height);
    stbi_image_free(data);
    return vec;
}

// Conversion to color temperature
double rgbToColorTemperature(rgba_t rgba) {
    // Normalize RGB values to [0, 1]
    double red = rgba.r / 255.0;
    double green = rgba.g / 255.0;
    double blue = rgba.b / 255.0;

    // Apply a gamma correction to RGB values (assumed gamma 2.2)
    red = (red > 0.04045) ? pow((red + 0.055) / 1.055, 2.4) : (red / 12.92);
    green = (green > 0.04045) ? pow((green + 0.055) / 1.055, 2.4) : (green / 12.92);
    blue = (blue > 0.04045) ? pow((blue + 0.055) / 1.055, 2.4) : (blue / 12.92);

    // Convert to XYZ color space
    double X = red * 0.4124 + green * 0.3576 + blue * 0.1805;
    double Y = red * 0.2126 + green * 0.7152 + blue * 0.0722;
    double Z = red * 0.0193 + green * 0.1192 + blue * 0.9505;

    // Calculate chromaticity coordinates
    double x = X / (X + Y + Z);
    double y = Y / (X + Y + Z);

    // Approximate color temperature using McCamy's formula
    double n = (x - 0.3320) / (0.1858 - y);
    double CCT = 449.0 * n*n*n + 3525.0 * n*n + 6823.3 * n + 5520.33;

    return CCT;
}


// Calculate the median from an image filename
double filename_to_median(const std::string& filename)
{
    int width, height;
    auto rgbadata = load_rgb(filename.c_str(), width, height);
    if (rgbadata.empty())
        return 0.0;
    
    std::vector<double> temperatures(rgbadata.size());

    //std:execution:par for data-parallel pixel processing
    std::transform(std::execution::par, rgbadata.begin(), rgbadata.end(), temperatures.begin(), rgbToColorTemperature);
    std::sort(temperatures.begin(), temperatures.end());
    auto median = temperatures.size() % 2 ? 0.5 * (temperatures[temperatures.size() / 2 - 1] + temperatures[temperatures.size() / 2]) : temperatures[temperatures.size() / 2];
    return median;
}

// Static sort -- REFERENCE ONLY
void static_sort(std::vector<std::string>& filenames)
{
    std::sort(filenames.begin(), filenames.end(), [](const std::string& lhs, const std::string& rhs) {
        return filename_to_median(lhs) < filename_to_median(rhs);
    });
}

sf::Vector2f SpriteScaleFromDimensions(const sf::Vector2u& textureSize, int screenWidth, int screenHeight)
{
    float scaleX = screenWidth / float(textureSize.x);
    float scaleY = screenHeight / float(textureSize.y);
    float scale = std::min(scaleX, scaleY);
    return { scale, scale };
}

//benchmark runner for specific thread counts
void runBenchmarkWithThreads(int numThreads, const std::vector<std::string>& filenames) {
    auto start = std::chrono::high_resolution_clock::now();

    int totalFiles = static_cast<int>(filenames.size());
    //handle edge case where thread count exceeds file count
    int activeThreads = std::min(numThreads, totalFiles);
    int chunkSz = (totalFiles + activeThreads - 1) / activeThreads;

    std::vector<std::thread> workers;
    workers.reserve(activeThreads);

    for (int i = 0; i < activeThreads; ++i) {
        int startIdx = i * chunkSz;
        int endIdx = std::min(startIdx + chunkSz, totalFiles);

        if (startIdx >= totalFiles) break;

        workers.emplace_back([startIdx, endIdx, &filenames]() {
            for (int j = startIdx; j < endIdx; ++j) {
                // call temp calculations
                volatile double temp = filename_to_median(filenames[j]);
                (void)temp;
            }
        });
    }

    //wait for all worker threads to finish
    for (auto& t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    
    std::cout << "[Benchmark] Threads: " << numThreads << " | Time: " << elapsed.count() << " ms\n";
}

//function to execute the full test suite across varied threads
void runThreadScalabilitySuite(const std::vector<std::string>& filenames) {
    std::cout << "AMDAHL'S LAW THREAD BENCHMARK \n";
    
    int threadCounts[] = {1, 2, 4, 8, 16};
    for (int tc : threadCounts) {
        runBenchmarkWithThreads(tc, filenames);
    }
}


int main()
{
    std::vector<std::string> imageFilenames;

    //application start time
    auto appStartTime = std::chrono::high_resolution_clock::now();

    std::srand(static_cast<unsigned int>(std::time(NULL)));

    // example folder to load images
    const char* image_folder = "images/unsorted";
    if (!fs::is_directory(image_folder))
    {
        printf("Directory \"%s\" not found: please make sure it exists, and if it's a relative path, it's under your WORKING directory\n", image_folder);
        return -1;
    }
    for (auto& p : fs::directory_iterator(image_folder))
        imageFilenames.push_back(p.path().u8string());

    if (imageFilenames.empty()) {
        std::cout <<"no images found. \n";
        return -1;
    }

    //thread scalability for graph data
    runThreadScalabilitySuite (imageFilenames);

    //when background sorting starts, when ui window opens
    auto uiReadyTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> startupElapsed = uiReadyTime - appStartTime;
    std::cout << "Performance - UI window opened and responsive in: " << startupElapsed.count() << "ms\n";

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //  YOUR CODE HERE INSTEAD, TO ORDER THE IMAGES IN A MULTI-THREADED MANNER WITHOUT BLOCKING  //
    
    //background threading and synchronisation

    //shared concurrency control primitives 
    std::mutex filenamesMutex;
    std::atomic<bool> isSorted{false};

    //background thread for sorting and I/O
    std::thread sortingWorker([&imageFilenames, &filenamesMutex, &isSorted]() {
        //total processing time
        auto sortStartTime = std::chrono::high_resolution_clock::now();
        //parallel execution for medians
        struct RatedFile {
            std::string filename;
            double medianTemp;
        };

        std::vector<RatedFile> ratedFiles(imageFilenames.size());

        //medians in parallel for each filename
        std::transform(std::execution::par, imageFilenames.begin(), imageFilenames.end(), ratedFiles.begin(), [](const std::string& path) {
            return RatedFile{path, filename_to_median(path)};
        });

        //sort in descending order by colour temp (hot to cold)
        std::sort(ratedFiles.begin(), ratedFiles.end(), [](const RatedFile& a, const RatedFile& b) {
            return a.medianTemp < b.medianTemp;
        });

        //under mutex lock, update shared imageFilenames vector
        {
            std::lock_guard<std::mutex> lock(filenamesMutex);
            for (size_t i = 0; i < ratedFiles.size(); ++i) {
                imageFilenames[i] = ratedFiles[i].filename;
            }
            isSorted.store(true);
        }
        std::cout << "Background thread - Sorting complete \n";

        std::cout << "\n sorted image results (hot to cold) \n";
        for (const auto& rf : ratedFiles) {
            std::cout << rf.filename << " -> " << rf.medianTemp;
        }

        auto sortEndTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> sortElapsed = sortEndTime - sortStartTime;
        std::cout << "Performance - background parallel sort complete in: " << sortElapsed.count() << "ms\n";

    });
    sortingWorker.detach();


    ///////////////////////////////////////////////////////////////////////////////////////////////

    // Define some constants
    const int gameWidth = 800;
    const int gameHeight = 600;

    int imageIndex = 0;

    // Create the window of the application
    sf::RenderWindow window(sf::VideoMode(gameWidth, gameHeight, 32), "Image Fever",
                            sf::Style::Titlebar | sf::Style::Close);
    window.setVerticalSyncEnabled(true);

    // Load an image to begin with, now protexted by mutex
    sf::Texture texture;
    {
        std::lock_guard<std::mutex> lock(filenamesMutex);
        if (!texture.loadFromFile(imageFilenames[imageIndex]))
            return EXIT_FAILURE;
    }

    sf::Sprite sprite (texture);

    // Make sure the texture fits the screen
    sprite.setScale(SpriteScaleFromDimensions(texture.getSize(),gameWidth,gameHeight));

    sf::Clock clock;
    while (window.isOpen())
    {
        // Handle events
        sf::Event event;
        while (window.pollEvent(event))
        {
            // Window closed or escape key pressed: exit
            if ((event.type == sf::Event::Closed) ||
               ((event.type == sf::Event::KeyPressed) && (event.key.code == sf::Keyboard::Escape)))
            {
                window.close();
                break;
            }
            
            // Window size changed, adjust view appropriately
            if (event.type == sf::Event::Resized)
            {
                sf::View view;
                view.setSize(gameWidth, gameHeight);
                view.setCenter(gameWidth/2.f, gameHeight/2.f);
                window.setView(view);
            }

            // Arrow key handling!
            if (event.type == sf::Event::KeyPressed)
            {
                // protected read/writes to share imageFilenames vector
                std::lock_guard<std::mutex> lock(filenamesMutex);
                if (imageFilenames.empty()) continue;

                // adjust the image index
                if (event.key.code == sf::Keyboard::Key::Left)
                    imageIndex = (imageIndex + imageFilenames.size() - 1) % imageFilenames.size();
                else if (event.key.code == sf::Keyboard::Key::Right)
                    imageIndex = (imageIndex + 1) % imageFilenames.size();
                
                // get image filename
                const auto& imageFilename = imageFilenames[imageIndex];
                //lock-free atomic check for window title status
                std::string title = isSorted.load() ? imageFilename : imageFilename + " [Sorting...]";
                // set it as the window title 
                window.setTitle(title);
                // ... and load the appropriate texture, and put it in the sprite
                if (texture.loadFromFile(imageFilename))
                {
                    sprite = sf::Sprite(texture);
                    sprite.setScale(SpriteScaleFromDimensions(texture.getSize(), gameWidth, gameHeight));
                }
            }
        }

        // Clear the window
        window.clear(sf::Color(0, 0, 0));
        // draw the sprite
        window.draw(sprite);
        // Display things on screen
        window.display();
    }

    return EXIT_SUCCESS;
}
