#include <cstdio>
#include <iostream>
#include <memory>
//#include <stdexcept>
#include <string>
#include <array>
#include <unordered_map>
#include <vector>
#include <functional>
#include <chrono>
#define DEBUG

struct FunctionPair {
	std::string left;
	std::string right;
	size_t leftHashVal;
	size_t rightHashVal;
	size_t combinedHashVal;
};

std::unordered_map<std::string, std::shared_ptr<FunctionPair>> pairs;
std::vector<std::shared_ptr<FunctionPair>> linearPairs;

struct FunctionNode {
	std::string name;
	size_t nameHashVal;
	//size_t uses = 0;
	std::vector<std::shared_ptr<FunctionNode>> list;
	std::vector<int> levels;
	std::vector<std::shared_ptr<FunctionPair>> pairs;
};

std::unordered_map<std::string, std::shared_ptr<FunctionNode>> functions;
std::vector<std::shared_ptr<FunctionNode>> linearFunctions;
int main(int argc, char *argv[])
{


	auto start = std::chrono::high_resolution_clock::now();
	// A floating point milliseconds type
	using FpSeconds =
	    std::chrono::duration<float, std::chrono::seconds::period>;

	static_assert(std::chrono::treat_as_floating_point<FpSeconds::rep>::value,
	              "Rep required to be floating point");

	std::string filePath = argc > 1 ? std::string(argv[1]) : "./test3/httpd.bc.orig";
	std::string command = "opt -print-callgraph " + filePath + " 2>&1 1>/dev/null";

	int minSupport = 3;
	float minConfidence = 65;
	if (argc >= 4) {
		minSupport = std::stoi(std::string(argv[2]));
		minConfidence = std::stof(std::string(argv[3]));
	}
	bool debug = false;
	bool timing = false;
	if (argc > 4 && std::string(argv[4]).compare("-d") == 0) {
		debug = true;
		timing = true;
	}

	if (argc > 4 && std::string(argv[4]).compare("-t") == 0) {
		timing = true;
	}
	bool enableInterProceduralAnalysis = false;
	int extraLevel = 0;
	if (argc > 5 && std::string(argv[4]).compare("-ipa") == 0) {
		enableInterProceduralAnalysis = true;
		extraLevel = std::stoi(std::string(argv[5]));
	}
	//Parse call graph.
	std::array<char, 128> buffer;
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);

	if (!pipe) {
		throw std::runtime_error("popen() failed!");
	}

	std::shared_ptr<FunctionNode> archetype;
	bool currentIsNullFunc = false;
	while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
		std::string line = std::string(buffer.data());
		if (line[2] == 'l') {
			currentIsNullFunc = false;
			auto start = line.find_first_of('\'') + 1;
			auto end = line.find_first_of('\'', start);
			if (start == std::string::npos || end == std::string::npos) {
				currentIsNullFunc = true;
				continue;
			}
			std::string name = line.substr(start, end - start);
			auto search = functions.find(name);

			if (search != functions.end()) {
				archetype = search->second;
			} else {
				archetype = std::make_shared<FunctionNode>();
				archetype->name = name;
				archetype->list.clear();
				archetype->levels.clear();
				archetype->nameHashVal = std::hash<std::string> {}(name);
				functions.insert({ name, archetype });
				linearFunctions.push_back(archetype);
			}
			//start = line.find_first_of("=") + 1;
			//size_t uses = 0;
			//uses = std::stoi(line.substr(start));
			//archetype->uses = uses;
		} else if (!currentIsNullFunc) {
			auto start = line.find_first_of('\'') + 1;
			auto end = line.find_first_of('\'', start);
			if (start == std::string::npos || end == std::string::npos) continue;
			std::string name = line.substr(start, end - start);
			auto search = functions.find(name);
			std::shared_ptr<FunctionNode> targetNode;
			if (search == functions.end()) {
				targetNode = std::make_shared<FunctionNode>();
				targetNode->name = name;
				targetNode->list.clear();
				targetNode->levels.clear();
				targetNode->nameHashVal = std::hash<std::string> {}(name);
				//targetNode->uses = 0;
				functions.insert({ name, targetNode });
				linearFunctions.push_back(targetNode);
			} else {
				targetNode = search->second;
			}
			bool duplicate = false;
			for (auto& i : archetype->list) {
				if (i->name.compare(name) == 0) {
					duplicate = true;
					break;
				}
			}
			if (!duplicate) {
				archetype->list.push_back(targetNode);
				if (enableInterProceduralAnalysis) archetype->levels.push_back(0);
			}
		}
		//std::cout << line;
	}
	archetype.reset();

	if (enableInterProceduralAnalysis) {
		//Here we add more functions if inter procedural analysis is needed.
		for (auto node : linearFunctions) {

			for (int iteration = 0; iteration < extraLevel; iteration++) {
				int start = 0;
				int size = node->list.size();
				for (auto currentIndex = start; currentIndex < size; currentIndex++) {
					auto currentNode = node->list[currentIndex];
					int currentLevel = node->levels[currentIndex];
					int childSize = currentNode->list.size();
					for (int childIndex = 0; childIndex < childSize; childIndex++) {
						currentNode = node->list[currentIndex];
						auto childNode = currentNode->list[childIndex];
						int childLevel = currentNode->levels[childIndex];
						if (childLevel != 0) break;
						bool missing = true;
						for (auto& compareNode : node->list) {
							if (compareNode->nameHashVal == childNode->nameHashVal) {
								missing = false;
							}
						}
						if (missing) {
							//std::cout << "Adding " << std::to_string(iteration) << "lvl: " << childNode->name << std::endl;
							auto newNode = std::shared_ptr<FunctionNode>(currentNode->list[childIndex]);
							node->list.push_back(newNode);
							node->levels.push_back(iteration + 1);
						}
					}
				}
				start = size;
				size = node->list.size();
			}
		}
	}
	for (auto& node : linearFunctions) {
		int size = node->list.size();
		for (auto a = 0; a < size - 1; a++) {
			for (auto b = a + 1; b < size; b++) {
				std::string as = node->list[a]->name;
				std::string bs = node->list[b]->name;
				std::string combine;
				bool comp = as.compare(bs) < 0;
				combine = comp ? as + bs : bs + as;
				auto search = pairs.find(combine);
				std::shared_ptr<FunctionPair> targetPair;
				if (search == pairs.end()) {
					targetPair = std::make_shared<FunctionPair>();
					targetPair->left = comp ? as : bs;
					targetPair->right = comp ? bs : as;
					targetPair->leftHashVal = comp ? node->list[a]->nameHashVal : node->list[b]->nameHashVal;
					targetPair->rightHashVal = comp ? node->list[b]->nameHashVal : node->list[a]->nameHashVal;
					targetPair->combinedHashVal = std::hash<std::string> {}(combine);
					pairs.insert({ combine, targetPair });
					linearPairs.push_back(targetPair);
				} else {
					targetPair = search->second;
				}
				node->pairs.push_back(targetPair);
			}
		}
	}

#ifdef DEBUG
	if (debug) {
		std::cout << "Pair construction done." << std::endl;
		for (const auto& i : functions) {
			const auto& func = i.second;
			if (func->pairs.empty()) continue;
			std::cout << "Name: " << func->name << ": " << std::to_string((int)func.use_count() - 2) /*<< ", Use Count captured from LLVM: " << std::to_string(func->uses - 1) */ << std::endl;
			std::cout << "Called: ";
			int index = 0;
			for (const auto& node : func->list) {
				if (enableInterProceduralAnalysis)std::cout << node->name << "=L" << std::to_string(func->levels[index]) << " | ";
				else std::cout << node->name << " | ";
				index++;
			}
			std::cout << std::endl << "Pairs: ";

			for (const auto& pair : func->pairs) {
				std::cout << "[" << pair->left << ", " << pair->right << "](" << std::to_string(pair.use_count() - 2) << ") | ";
			}
			std::cout << std::endl << std::endl;
		}
	}
#endif
	for (const auto& func : linearFunctions) {
		for (const auto& node : func->list) {
			for (const auto& pair : linearPairs) {
				if (pair->leftHashVal == node->nameHashVal || pair->rightHashVal == node->nameHashVal) {
					bool missing = true;
					for (const auto& localPair : func->pairs) {
						if (localPair->combinedHashVal == pair->combinedHashVal) {
							missing = false;
							break;
						}
					}
					if (missing) {
						int support = pair.use_count() - 2;
						int localSupport = node.use_count() - 2;
						float confidence = (float)support / localSupport * 100.0;
						if (support >= minSupport && confidence >= minConfidence) {

							std::printf("bug: %s in %s, pair: (%s, %s), support: %d, confidence: %.2f%%\n",
							            node->name.c_str(), func->name.c_str(),
							            pair->left.c_str(), pair->right.c_str(),
							            support,
							            confidence
							           );

						}
					}
				}
			}
		}
	}
	auto stop = std::chrono::high_resolution_clock::now();
	float time = FpSeconds(stop - start).count();
	if (timing) std::printf("Used time: %.2f\n", time);
	return 0;
}