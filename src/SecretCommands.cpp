#include "SecretCommands.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "Logging.h"
#include "KubeInterface.h"

//conforms to the interface of rapidjson::GenericStringBuffer<UTF8<char>> but
//tries to only keep data in buffers which will be automatically cleared
struct SecretStringBuffer{
	using Ch=rapidjson::UTF8<char>::Ch;
	
	SecretStringBuffer():data(SecretData(32)),size(0){}
	
	void Put(Ch c){
		if(size==data.dataSize)
			Reserve(2*size);
		*(data.data.get()+size)=c;
		size++;
	}
	void PutUnsafe(Ch c){
		//not necessarily optimally fast, but just call safe version
		Put(c);
	}
	void Flush(){ /*do nothing*/ }
	void Clear(){
		data=SecretData(32);
		size=0;
	}
	void ShrinkToFit(){
		SecretData newData(size);
		std::copy_n(data.data.get(),size,newData.data.get());
		data=std::move(newData);
	}
	void Reserve(std::size_t count){
		if(count>data.dataSize){
			SecretData newData(count);
			std::copy_n(data.data.get(),size,newData.data.get());
			data=std::move(newData);
		}
	}
	Ch* Push(std::size_t count){
		Reserve(size+count);
		Ch* ret=data.data.get()+size;
		size+=count;
		return ret;
	}
	Ch* PushUnsafe(std::size_t count){
		//not necessarily optimally fast, but just call safe version
		return PushUnsafe(count);
	}
	void Pop(std::size_t count){
		size-=count;
	}
	const Ch* GetString() const{
		return data.data.get();
	}
	std::size_t GetSize() const{
		return size;
	}
	std::size_t GetLength() const{
		return size;
	}
	
	SecretData data;
	///The current amount of data curently in use, while data.dataSize is the 
	///total capacity
	std::size_t size;
};

crow::response listSecrets(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list secrets");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list clusters
	
	auto voRaw = req.url_params.get("vo");
	auto clusterRaw = req.url_params.get("cluster");
	
	if(!voRaw)
		return crow::response(400,generateError("A VO must be spcified"));
	std::string cluster;
	if(clusterRaw)
		cluster=clusterRaw;
	
	//get information on the owning VO, needed to look up services, etc.
	const VO vo=store.getVO(voRaw);
	if(!vo)
		return crow::response(404,generateError("VO not found"));
	
	//only admins or members of a VO may list its secrets
	if(!user.admin && !store.userInVO(user.id,vo.id))
		return crow::response(403,generateError("Not authorized"));
	
	std::vector<Secret> secrets=store.listSecrets(vo.id,cluster);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(secrets.size(), alloc);
	
	for(const Secret& secret : secrets){
		rapidjson::Value secretResult(rapidjson::kObjectType);
		secretResult.AddMember("apiVersion", "v1alpha1", alloc);
		secretResult.AddMember("kind", "Secret", alloc);
		rapidjson::Value secretData(rapidjson::kObjectType);
		secretData.AddMember("id", secret.id, alloc);
		secretData.AddMember("name", secret.name, alloc);
		secretData.AddMember("vo", store.getVO(secret.vo).name, alloc);
		secretData.AddMember("cluster", store.getCluster(secret.cluster).name, alloc);
		secretData.AddMember("created", secret.ctime, alloc);
		secretResult.AddMember("metadata", secretData, alloc);
		resultItems.PushBack(secretResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	
	return crow::response(to_string(result));
}

crow::response createSecret(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a secret");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//unpack the target cluster info
	rapidjson::Document body;
	try{
		body.Parse(req.body);
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	
	if(body.IsNull()) {
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body.HasMember("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for metadata"));
	
	if(!body["metadata"].HasMember("name"))
		return crow::response(400,generateError("Missing secret name in request"));
	if(!body["metadata"]["name"].IsString())
		return crow::response(400,generateError("Incorrect type for secret name"));
	if(!body["metadata"].HasMember("vo"))
		return crow::response(400,generateError("Missing VO ID in request"));
	if(!body["metadata"]["vo"].IsString())
		return crow::response(400,generateError("Incorrect type for VO ID"));
	if(!body["metadata"].HasMember("cluster"))
		return crow::response(400,generateError("Missing cluster ID in request"));
	if(!body["metadata"]["cluster"].IsString())
		return crow::response(400,generateError("Incorrect type for cluster ID"));
	
	if(body.HasMember("contents") && body.HasMember("copyFrom"))
		return crow::response(400,generateError("Secret contents and copy source cannot both be specified"));
	if(!body.HasMember("contents") && !body.HasMember("copyFrom"))
		return crow::response(400,generateError("Missing secret contents or source in request"));
	if(body.HasMember("contents") && !body["contents"].IsObject())
		return crow::response(400,generateError("Incorrect type for contents"));
	if(body.HasMember("copyFrom") && !body["copyFrom"].IsString())
		return crow::response(400,generateError("Incorrect type for copyFrom"));
	
	//contents may not be completely arbitrary key-value pairs; 
	//the values need to be strings, the keys need to meet kubernetes requirements
	const static std::string allowedKeyCharacters="-._0123456789"
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	if(body.HasMember("contents")){
		for(const auto& member : body["contents"].GetObject()){
			if(!member.value.IsString())
				return crow::response(400,generateError("Secret value is not a string"));
			if(member.name.GetStringLength()==0)
				return crow::response(400,generateError("Secret keys may not be empty"));
			if(member.name.GetStringLength()>253)
				return crow::response(400,generateError("Secret keys may be no more than 253 characters"));
			if(std::string(member.name.GetString())
			   .find_first_not_of(allowedKeyCharacters)!=std::string::npos)
				return crow::response(400,generateError("Secret key does not match [-._a-zA-Z0-9]+"));
		}
	}
	
	Secret secret;
	secret.id=idGenerator.generateSecretID();
	secret.name=body["metadata"]["name"].GetString();
	secret.vo=body["metadata"]["vo"].GetString();
	secret.cluster=body["metadata"]["cluster"].GetString();
	secret.ctime=timestamp();
	
	//https://kubernetes.io/docs/concepts/overview/working-with-objects/names/
	if(secret.name.size()>253)
		return crow::response(400,generateError("Secret name too long"));
	if(secret.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=std::string::npos)
		return crow::response(400,generateError("Secret name contains an invalid character"));
	
	VO vo=store.getVO(secret.vo);
	if(!vo)
		return crow::response(404,generateError("VO not found"));
	//canonicalize VO
	secret.vo=vo.id;
	
	//only members of a VO may install secrets for it
	if(!store.userInVO(user.id,vo.id))
		return crow::response(403,generateError("Not authorized"));
	
	Cluster cluster=store.getCluster(secret.cluster);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	//canonicalize cluster
	secret.cluster=cluster.id;
	
	//VOs may only install secrets on clusters which they own or to which 
	//they've been granted access
	if(vo.id!=cluster.owningVO && !store.voAllowedOnCluster(vo.id,cluster.id))
		return crow::response(403,generateError("Not authorized"));
	
	//check that name is not in use
	Secret existing=store.findSecretByName(vo.id,secret.cluster,secret.name);
	if(existing)
		return crow::response(400,generateError("A secret with the same name already exists"));
	
	if(body.HasMember("contents")){ //Re-serialize the contents and encrypt
		SecretStringBuffer buf;
		rapidjson::Writer<SecretStringBuffer> writer(buf);
		rapidjson::Document tmp(rapidjson::kObjectType);
		body["contents"].Accept(writer);
		//swizzle size and capacity so we encrypt only the useful data, but put
		//things back when we're done
		const std::size_t capacity=buf.data.dataSize;
		struct fixCap{
			const std::size_t& cap;
			SecretData& data;
			fixCap(const std::size_t& cap, SecretData& data):cap(cap),data(data){}
			~fixCap(){ data.dataSize=cap; }
		} fix(capacity,buf.data);
		buf.data.dataSize=buf.size;
		secret.data=store.encryptSecret(buf.data);
	}
	else{ //try to copy contents from an existing secret
		std::string sourceID=body["copyFrom"].GetString();
		existing=store.getSecret(sourceID);
		if(!existing)
			return crow::response(404,generateError("The specified source secret does not exist"));
		//make sure that the requesting user has access to the source secret
		if(!store.userInVO(user.id,existing.vo))
			return crow::response(403,generateError("Not authorized"));
		secret.data=existing.data;
		//Unfortunately, we _also_ need to decrypt the secret in order to pass
		//its data to Kubernetes. 
		SecretData secretData=store.decryptSecret(existing);
		rapidjson::Document contents(rapidjson::kObjectType,&body.GetAllocator());
		contents.Parse(secretData.data.get(),secretData.dataSize);
		body.AddMember("contents",contents,body.GetAllocator());
	}
	secret.valid=true;
	
	log_info("Storing secret " << secret << " for " << vo  << " on " << cluster);
	
	//put secret into the DB
	bool success=store.addSecret(secret);
	if(!success)
		return crow::response(500,generateError("Failed to store secret to the persistent store"));
	
	//put secret into kubernetes
	{
		auto configPath=store.configPathForCluster(cluster.id);
		
		try{
			kubernetes::kubectl_create_namespace(*configPath, vo);
		}
		catch(std::runtime_error& err){
			store.removeSecret(secret.id);
			return crow::response(500,generateError(err.what()));
		}
		
		std::vector<std::string> arguments={"create","secret","generic",
		                                    secret.name,"--namespace",vo.namespaceName()};
		for(const auto& member : body["contents"].GetObject()){
			arguments.push_back("--from-literal");
			arguments.push_back(member.name.GetString()+std::string("=")+member.value.GetString());
		}
		try{
			kubernetes::kubectl_create_namespace(*configPath,vo);
		}catch(std::runtime_error& err){
			if(err.what()!=std::string("Namespace creation failed"))
				throw;
		}
		auto result=kubernetes::kubectl(*configPath, arguments);
		
		if(result.status){
			std::string errMsg="Failed to store secret to kubernetes: "+result.error;
			log_error(errMsg);
			//if installation fails, remove from the database again
			store.removeSecret(secret.id);
			return crow::response(500,generateError(errMsg));
		}
	}
	
	log_info("Created " << secret << " on " << cluster << " owned by " << vo 
	         << " on behalf of " << user);
	
	//compose response
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "Secret", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", secret.id, alloc);
	metadata.AddMember("name", secret.name, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	return crow::response(to_string(result));
}

crow::response deleteSecret(PersistentStore& store, const crow::request& req,
                            const std::string& secretID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete a secret");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	Secret secret=store.getSecret(secretID);
	if(!secret)
		return crow::response(404,generateError("Secret not found"));
	
	//only members of a VO may delete its secrets
	if(!store.userInVO(user.id,secret.vo))
		return crow::response(403,generateError("Not authorized"));
	bool force=(req.url_params.get("force")!=nullptr);
	
	auto err=internal::deleteSecret(store,secret,force);
	if(!err.empty())
		crow::response(500,generateError(err));
	return crow::response(200);
}

namespace internal{
std::string deleteSecret(PersistentStore& store, const Secret& secret, bool force){
	log_info("Deleting " << secret);
	//remove from kubernetes
	{
		VO vo=store.findVOByID(secret.vo);
		try{
			auto configPath=store.configPathForCluster(secret.cluster);
			auto result=kubernetes::kubectl(*configPath,
			  {"delete","secret",secret.name,"--namespace",vo.namespaceName()});
			if(result.status){
				log_error("kubectl delete secret failed: " << result.error);
				if(!force)
					return "Failed to delete secret from kubernetes";
				else
					log_info("Forcing deletion of " << secret << " in spite of kubectl error");
			}
		}
		catch(std::runtime_error& e){
			if(!force)
				return "Failed to delete secret from kubernetes";
			else
				log_info("Forcing deletion of " << secret << " in spite of error");
		}
	}
	
	//remove from the database
	bool success=store.removeSecret(secret.id);
	if(!success){
		log_error("Failed to delete " << secret << " from persistent store");
		return "Failed to delete secret from database";
	}
	return "";
}
}

crow::response getSecret(PersistentStore& store, const crow::request& req,
                         const std::string& secretID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to get a secret");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	Secret secret=store.getSecret(secretID);
	if(!secret)
		return crow::response(404,generateError("Secret not found"));
	
	//only members of a VO may view its secrets
	if(!store.userInVO(user.id,secret.vo))
		return crow::response(403,generateError("Not authorized"));
	
	log_info("Sending " << secret << " to " << user);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "Secret", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", secret.id, alloc);
	metadata.AddMember("name", secret.name, alloc);
	metadata.AddMember("vo", store.getVO(secret.vo).name, alloc);
	metadata.AddMember("cluster", store.getCluster(secret.cluster).name, alloc);
	metadata.AddMember("created", secret.ctime, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	rapidjson::Document contents;
	try{
		auto secretData=store.decryptSecret(secret);
		contents.Parse(secretData.data.get(), secretData.dataSize);
		result.AddMember("contents",contents,alloc);
	} catch(std::runtime_error& err){
		log_error("Secret decryption failed: " << err.what());
		return crow::response(500,generateError("Secret decryption failed"));
	}
	
	return crow::response(to_string(result));
}