/****
DIAMOND protein aligner - Markov Clustering Module
Copyright (C) 2020 QIAGEN A/S (Aarhus, Denmark)
Code developed by Patrick Ettenhuber <patrick.ettenhuber@qiagen.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
****/

#include <algorithm>
#include <stdio.h>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

template<typename T> class Node {
	Node<T>* parent = this;
	uint32_t r = 0;
public:
	virtual ~Node(){};
	virtual Node<T>* getParent(){
		return parent;
	}
	virtual void setParent(Node<T>* p){
		parent = p;
	}

	virtual const T* getValue() = 0;

	virtual uint32_t getCount(){
		return r;
	}
	virtual void incrementCount(){
		r++;
	}
};

template<typename T> class IntegralNode: public Node<T> {
private:
	T value;
public:
	virtual ~IntegralNode(){};
	IntegralNode<T>(T v){
		value = v;
	}
	virtual const T* getValue(){
		return &value;
	}
};

template<typename T> class TypeNode: public Node<T> {
private:
	const T* value;
	uint32_t r;
public:
	virtual ~TypeNode(){};
	TypeNode<T>(const T* v){
		value = v;
	}
	virtual const T* getValue(){
		return value;
	}
};

template<typename T> class LazyDisjointSet {
private:
	virtual Node<T>* get(const T i) = 0;
	virtual Node<T>* get(const T* i) = 0;
	virtual uint64_t size() = 0;
	virtual vector<Node<T>*>* getNodes() = 0;
public:
	virtual ~LazyDisjointSet(){};

	virtual Node<T>* getRoot(Node<T>* x) {
		if (x->getParent() != x) {
			// flatten the tree while traversing
			x->setParent(getRoot(x->getParent()));
		}
		return x->getParent();
	}

	virtual void merge(Node<T>* x, Node<T>* y) {
		if (x != y) {
			Node<T>* rootX = getRoot(x);
			Node<T>* rootY = getRoot(y);
			if (rootX != rootY) {
				if (rootX->getCount() < rootY->getCount()) {
					rootX->setParent(rootY);
				}
				else if (rootX->getCount() > rootY->getCount()) {
					rootY->setParent(rootX);
				}
				else {
					rootX->incrementCount();
					rootY->setParent(rootX);
				}
			}
		}
	}

	virtual void merge(const T* x, const T* y) {
		merge(get(x), get(y));
	}

	void merge(T x, T y) {
		merge(get(x), get(y)); 
	}

	virtual vector<unordered_set<T>> getListOfSets() {
		unordered_map<const T*, unordered_set<T>*> map;
		for (Node<T>* n : *(getNodes())) {
			if (n == nullptr) {
				continue;
			}
			const T* r = getRoot(n)->getValue();
			auto it = map.find(r);
			T v = *(n->getValue());
			if (it == map.end()) {
				it = map.emplace(r, new unordered_set<T>()).first;
			}
			it->second->emplace(v);
		}
		vector<unordered_set<T>> listOfSets;
		for(auto el=map.begin(); el != map.end(); el++){
			listOfSets.emplace_back(move(*(el->second)));
		}
		return listOfSets;
	}
};

template<typename T> class LazyDisjointIntegralSet : public LazyDisjointSet<T> {
private:
	vector<Node<T>*> nodes;

	virtual Node<T>* get(T i){
		assert(i >= 0 && nodes.size() > i);
		if(nodes[i] == nullptr){
			nodes[i] = new IntegralNode<T>(i);
		}
		return nodes[i];
	}

	virtual Node<T>* get(const T* i) {
		return get(*i);
	}

	virtual uint64_t size(){
		return nodes.size();
	}
	virtual vector<Node<T>*>* getNodes(){
		return &nodes;
	}

public:
	static_assert(std::is_integral<T>::value, "T needs to be an integral type");

	LazyDisjointIntegralSet<T>(T size) {
		nodes = vector<Node<T>*>(size, nullptr);
	}

	virtual ~LazyDisjointIntegralSet<T>() {
		for(Node<T>* n : nodes){
			if (n != nullptr){
				delete n;
				n = nullptr;
			}
		}
		nodes.clear();
	}
};

template<typename T> class LazyDisjointTypeSet : public LazyDisjointSet<T> {
private:
	unordered_set<T>* elements;
	vector<Node<T>*> nodes;
	unordered_map<const T*, uint64_t> mapping;
	bool isIntegralAndConsecutive;

	virtual Node<T>* get(const T* i) {
		auto found = mapping.find(i);
		assert(found != mapping.end());
		return nodes[found->second];
	}

	virtual Node<T>* get(T i) {
		auto found = elements->find(i);
		assert(found != elements->end());
		return get(&(*found));
	}

	virtual uint64_t size(){
		return nodes.size();
	}
	virtual vector<Node<T>*>* getNodes(){
		return &nodes;
	}

public:
	LazyDisjointTypeSet<T>(unordered_set<T>* s) {
		// note that the structure is backed by the set, so it should not be modified
		isIntegralAndConsecutive = false;
		elements = s;
		nodes = vector<Node<T>*>(elements->size());
		mapping = unordered_map<const T*, uint64_t>(elements->size());
		for(auto e = elements->begin(); e != elements->end(); e++){
			mapping[&(*e)] = nodes.size();
			Node<T>* n = new TypeNode<T>(&(*e));
			nodes.push_back(n);
		}
	}
	virtual ~LazyDisjointTypeSet<T>() {
		mapping.clear();
		for(Node<T>* n : nodes){
			if (n != nullptr){
				delete n;
				n = nullptr;
			}
		}
		nodes.clear();
	}
};
