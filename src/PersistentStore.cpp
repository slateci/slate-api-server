#include <PersistentStore.h>

#include <aws/core/utils/Outcome.h>
#include <aws/dynamodb/model/DeleteItemRequest.h>
#include <aws/dynamodb/model/GetItemRequest.h>
#include <aws/dynamodb/model/PutItemRequest.h>
#include <aws/dynamodb/model/QueryRequest.h>
#include <aws/dynamodb/model/ScanRequest.h>
#include <aws/dynamodb/model/UpdateItemRequest.h>

PersistentStore::PersistentStore(Aws::Auth::AWSCredentials credentials, 
				Aws::Client::ClientConfiguration clientConfig):
	dbClient(std::move(credentials),std::move(clientConfig)),
	userTableName("SLATE_users"),
	voTableName("SLATE_VOs")
{}

bool PersistentStore::addUser(const User& user){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(userTableName)
	.WithItem({
		{"ID",AttributeValue(user.id)},
		{"sortKey",AttributeValue(user.id)},
		{"name",AttributeValue(user.name)},
		{"globusID",AttributeValue(user.globusID)},
		{"token",AttributeValue(user.token)},
		{"email",AttributeValue(user.email)},
		{"admin",AttributeValue().SetBool(user.admin)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return false;
	}
	
	return true;
}

User PersistentStore::getUser(const std::string& id){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(userTableName)
								  .WithKey({{"ID",AttributeValue(id)},
	                                        {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return User();
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return User{};
	User user;
	user.valid=true;
	user.id=id;
	user.name=item.find("name")->second.GetS();
	user.email=item.find("email")->second.GetS();
	user.token=item.find("token")->second.GetS();
	user.globusID=item.find("globusID")->second.GetS();
	user.admin=item.find("admin")->second.GetBool();
	return user;
}

User PersistentStore::findUserByToken(const std::string& token){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::QueryRequest()
	.WithTableName(userTableName)
	.WithIndexName("ByToken")
	.WithKeyConditionExpression("#token = :tok_val")
	.WithExpressionAttributeNames({
		{"#token","token"}
	})
	.WithExpressionAttributeValues({
		{":tok_val",AttributeValue(token)}
	});
	auto outcome=dbClient.Query(request);
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return User();
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()!=1){ //TODO: further action for >1 case?
		return User();
	}
	
	User user;
	user.valid=true;
	user.token=token;
	user.id=queryResult.GetItems().front().find("ID")->second.GetS();
	user.admin=queryResult.GetItems().front().find("admin")->second.GetBool();
	return user;
}

User PersistentStore::findUserByGlobusID(const std::string& globusID){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
								.WithTableName(userTableName)
								.WithIndexName("ByGlobusID")
								.WithKeyConditionExpression("#globusID = :id_val")
								.WithExpressionAttributeNames({{"#globusID","globusID"}})
								.WithExpressionAttributeValues({{":id_val",AV(globusID)}})
								);
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return User();
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()!=1){ //TODO: further action for >1 case?
		return User();
	}
	
	User user;
	user.valid=true;
	user.id=queryResult.GetItems().front().find("ID")->second.GetS();
	user.token=queryResult.GetItems().front().find("token")->second.GetS();
	user.globusID=globusID;
	return user;
}

bool PersistentStore::updateUser(const User& user){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
	auto outcome=dbClient.UpdateItem(Aws::DynamoDB::Model::UpdateItemRequest()
	                                 .WithTableName(userTableName)
									 .WithKey({{"ID",AV(user.id)},
	                                           {"sortKey",AV(user.id)}})
	                                 .WithAttributeUpdates({
	                                            {"name",AVU().WithValue(AV(user.name))},
	                                            {"globusID",AVU().WithValue(AV(user.globusID))},
	                                            {"token",AVU().WithValue(AV(user.token))},
	                                            {"email",AVU().WithValue(AV(user.email))},
	                                            {"admin",AVU().WithValue(AV().SetBool(user.admin))}
	                                 }));
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return false;
	}
	
	return true;
}

bool PersistentStore::removeUser(const std::string& id){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(userTableName)
								     .WithKey({{"ID",AttributeValue(id)},
	                                           {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return false;
	}
	return true;
}

std::vector<User> PersistentStore::listUsers(){
	std::vector<User> collected;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(userTableName);
	//request.SetAttributesToGet({"ID","name","email"});
	request.SetFilterExpression("attribute_exists(email)");
	bool keepGoing=false;
	
	do{
		auto outcome=dbClient.Scan(request);
		if(!outcome.IsSuccess()){
			//TODO: more principled logging or reporting of the nature of the error
			auto err=outcome.GetError();
			std::cerr << err.GetMessage() << std::endl;
			return collected;
		}
		const auto& result=outcome.GetResult();
		//set up fetching the next page if necessary
		if(!result.GetLastEvaluatedKey().empty()){
			keepGoing=true;
			request.SetExclusiveStartKey(result.GetLastEvaluatedKey());
		}
		else
			keepGoing=false;
		//collect results from this page
		for(const auto& item : result.GetItems()){
			User user;
			user.valid=true;
			user.id=item.find("ID")->second.GetS();
			user.name=item.find("name")->second.GetS();
			user.email=item.find("email")->second.GetS();
			collected.push_back(user);
		}
	}while(keepGoing);
	return collected;
}

bool PersistentStore::addUserToVO(const std::string& uID, const std::string voID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(userTableName)
	.WithItem({
		{"ID",AttributeValue(uID)},
		{"sortKey",AttributeValue(uID+":"+voID)},
		{"voID",AttributeValue(voID)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return false;
	}
	
	return true;
}

bool PersistentStore::removeUserFromVO(const std::string& uID, const std::string& voID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(userTableName)
								     .WithKey({{"ID",AttributeValue(uID)},
	                                           {"sortKey",AttributeValue(uID+":"+voID)}}));
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return false;
	}
	return true;
}

std::vector<std::string> PersistentStore::getUserVOMemberships(const std::string& uID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::QueryRequest()
	.WithTableName(userTableName)
	.WithKeyConditionExpression("#id = :id AND begins_with(#sortKey,:prefix)")
	.WithExpressionAttributeNames({
		{"#id","ID"},
		{"#sortKey","sortKey"}
	})
	.WithExpressionAttributeValues({
		{":id",AttributeValue(uID)},
		{":prefix",AttributeValue(uID+":VO")}
	});
	auto outcome=dbClient.Query(request);
	std::vector<std::string> vos;
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return vos;
	}
	
	const auto& queryResult=outcome.GetResult();
	for(const auto& item : queryResult.GetItems()){
		if(item.count("voID"))
		vos.push_back(item.find("voID")->second.GetS());
	}
	return vos;
}

//----

bool PersistentStore::addVO(const VO& vo){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.PutItem(Aws::DynamoDB::Model::PutItemRequest()
	                              .WithTableName(voTableName)
	                              .WithItem({{"ID",AV(vo.id)},
	                                         {"sortKey",AV(vo.id)},
	                                         {"name",AV(vo.name)}
	                              }));
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return false;
	}
	
	return true;
}

bool PersistentStore::removeVO(const std::string& voID){
	using Aws::DynamoDB::Model::AttributeValue;
	
	//delete all memberships in the VO
	for(auto uID : getMembersOfVO(voID)){
		if(!removeUserFromVO(uID,voID))
			return false;
	}
	
	//delete the VO record itself
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(voTableName)
								     .WithKey({{"ID",AttributeValue(voID)},
	                                           {"sortKey",AttributeValue(voID)}}));
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return false;
	}
	return true;
}

std::vector<std::string> PersistentStore::getMembersOfVO(const std::string voID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
	                            .WithTableName(userTableName)
	                            .WithIndexName("ByVO")
	                            .WithKeyConditionExpression("#voID = :id_val")
	                            .WithExpressionAttributeNames({{"#voID","voID"}})
								.WithExpressionAttributeValues({{":id_val",AttributeValue(voID)}})
	                            );
	std::vector<std::string> users;
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return users;
	}
	const auto& queryResult=outcome.GetResult();
	users.reserve(queryResult.GetCount());
	for(const auto& item : queryResult.GetItems())
		users.push_back(item.find("ID")->second.GetS());
	
	return users;
}

std::vector<VO> PersistentStore::listVOs(){
	std::vector<VO> collected;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(voTableName);
	request.SetFilterExpression("attribute_exists(#name)");
	request.SetExpressionAttributeNames({{"#name","name"}});
	bool keepGoing=false;
	
	do{
		auto outcome=dbClient.Scan(request);
		if(!outcome.IsSuccess()){
			//TODO: more principled logging or reporting of the nature of the error
			auto err=outcome.GetError();
			std::cerr << err.GetMessage() << std::endl;
			return collected;
		}
		const auto& result=outcome.GetResult();
		//set up fetching the next page if necessary
		if(!result.GetLastEvaluatedKey().empty()){
			keepGoing=true;
			request.SetExclusiveStartKey(result.GetLastEvaluatedKey());
		}
		else
			keepGoing=false;
		//collect results from this page
		for(const auto& item : result.GetItems()){
			VO vo;
			vo.valid=true;
			vo.id=item.find("ID")->second.GetS();
			vo.name=item.find("name")->second.GetS();
			collected.push_back(vo);
		}
	}while(keepGoing);
	return collected;
}
